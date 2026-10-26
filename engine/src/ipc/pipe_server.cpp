#include "ipc/pipe_server.h"
#include "ipc/framing.h"
#include "opsec/process_guard.h"

#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601 // CancelIoEx 需要 Vista+
#  endif
#  include <windows.h>
#endif

namespace aim::ipc {

PipeServer::~PipeServer() { Stop(); }

bool PipeServer::Start(const std::string& pipe_name, Callback cb, std::string* err) {
    if (running_.load()) {
        if (err) *err = "pipe server already running";
        return false;
    }
    if (pipe_name.empty()) {
        if (err) *err = "pipe name empty";
        return false;
    }
    name_ = pipe_name;
    cb_ = std::move(cb);
    running_.store(true);
    thread_ = std::thread([this] { Loop(); });
    return true;
}

void PipeServer::Stop() {
    running_.store(false);
#ifdef _WIN32
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (pipe_) {
            HANDLE h = static_cast<HANDLE>(pipe_);
            CancelIoEx(h, nullptr);
            CloseHandle(h);
            pipe_ = nullptr;
        }
    }
#endif
    if (thread_.joinable()) thread_.join();
}

#ifdef _WIN32
void PipeServer::Loop() {
    // M9 L2（§5.12.2）：线程名中性化（必须在被创建的线程内设置）
    if (!thread_name_.empty()) {
        opsec::ProcessGuard::SetNeutralThreadName(thread_name_.c_str());
    }
    const std::string full = "\\\\.\\pipe\\" + name_;

    while (running_.load()) {
        HANDLE h = CreateNamedPipeA(
            full.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) break;

        {
            std::lock_guard<std::mutex> lk(mtx_);
            pipe_ = h;
        }

        const BOOL connected = ConnectNamedPipe(h, nullptr);
        const DWORD last_error = GetLastError();

        if (!running_.load()) {
            std::lock_guard<std::mutex> lk(mtx_);
            if (pipe_ == h) {
                DisconnectNamedPipe(h);
                CloseHandle(h);
                pipe_ = nullptr;
            }
            break;
        }

        if (connected || last_error == ERROR_PIPE_CONNECTED) {
            // 持久连接：同一连接上处理多帧请求，直到客户端关闭或 Stop。
            while (running_.load()) {
                uint8_t hdr[4];
                DWORD got = 0;
                if (!ReadFile(h, hdr, 4, &got, nullptr) || got != 4) break;

                const uint32_t len =
                    static_cast<uint32_t>(hdr[0]) |
                    (static_cast<uint32_t>(hdr[1]) << 8) |
                    (static_cast<uint32_t>(hdr[2]) << 16) |
                    (static_cast<uint32_t>(hdr[3]) << 24);
                if (len > kMaxFrameBytes) break;

                std::string payload(len, '\0');
                DWORD rd = 0;
                if (len > 0 &&
                    (!ReadFile(h, payload.data(), len, &rd, nullptr) || rd != len)) {
                    break;
                }

                std::string resp = cb_ ? cb_(payload) : std::string();
                const std::string frame = EncodeFrame(resp);
                DWORD wr = 0;
                if (!WriteFile(h, frame.data(), static_cast<DWORD>(frame.size()), &wr, nullptr)) {
                    break;
                }
                FlushFileBuffers(h);
            }
        }

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (pipe_ == h) {
                DisconnectNamedPipe(h);
                CloseHandle(h);
                pipe_ = nullptr;
            }
        }
    }
}
#else
void PipeServer::Loop() { running_.store(false); }
#endif

} // namespace aim::ipc
