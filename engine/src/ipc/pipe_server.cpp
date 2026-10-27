#include "ipc/pipe_server.h"
#include "ipc/framing.h"
#include "ipc/protocol.h"
#include "opsec/process_guard.h"

#include <chrono>

#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601 // CancelIoEx 需要 Vista+
#  endif
#  include <windows.h>
#  include <sddl.h> // ConvertStringSecurityDescriptorToSecurityDescriptorA
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
    // 只取消 I/O，不关句柄：句柄由各自的持有线程在阻塞调用返回后关闭。
    // 提前 CloseHandle 会让仍在 ConnectNamedPipe/ReadFile 的线程操作已释放的句柄值。
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (accept_pipe_) {
            CancelIoEx(static_cast<HANDLE>(accept_pipe_), nullptr);
        }
        for (void* h : conns_) {
            CancelIoEx(static_cast<HANDLE>(h), nullptr);
        }
    }
#endif
    if (thread_.joinable()) thread_.join();

    // accept 线程退出后不会再有新连接；等工作线程把在途请求处理完并自行关闭句柄
    std::vector<Worker> pending;
    {
        std::lock_guard<std::mutex> lk(workers_mtx_);
        pending.swap(workers_);
    }
    for (auto& w : pending) {
        if (w.th.joinable()) w.th.join();
    }
}

#ifdef _WIN32

void PipeServer::SweepFinishedWorkers() {
    for (auto it = workers_.begin(); it != workers_.end();) {
        if (it->done && it->done->load()) {
            if (it->th.joinable()) it->th.join();
            it = workers_.erase(it);
        } else {
            ++it;
        }
    }
}

void PipeServer::ServeConnection(void* handle) {
    HANDLE h = static_cast<HANDLE>(handle);
    while (running_.load()) {
        uint8_t hdr[4];
        DWORD got = 0;
        if (!ReadFile(h, hdr, 4, &got, nullptr) || got != 4) break;

        const uint32_t len = static_cast<uint32_t>(hdr[0]) |
                             (static_cast<uint32_t>(hdr[1]) << 8) |
                             (static_cast<uint32_t>(hdr[2]) << 16) |
                             (static_cast<uint32_t>(hdr[3]) << 24);
        if (len > kMaxFrameBytes) {
            // 超限不再静默断连：回一条明确的错误帧，客户端能区分"请求太大"与"引擎挂了"
            Response err;
            err.code = 413;
            err.msg = "request too large: " + std::to_string(len) + " > " +
                      std::to_string(kMaxFrameBytes);
            const std::string frame = EncodeFrame(Serialize(err));
            DWORD wr = 0;
            WriteFile(h, frame.data(), static_cast<DWORD>(frame.size()), &wr, nullptr);
            // 必须 flush 再关：字节模式管道在 CloseHandle 前未刷出的数据会被丢弃，
            // 客户端就只看得到"连接被关闭"，拿不到这条 413。
            FlushFileBuffers(h);
            break;
        }

        std::string payload(len, '\0');
        DWORD rd = 0;
        if (len > 0 && (!ReadFile(h, payload.data(), len, &rd, nullptr) || rd != len)) break;

        std::string resp = cb_ ? cb_(payload) : std::string();
        const std::string frame = EncodeFrame(resp);
        DWORD wr = 0;
        if (!WriteFile(h, frame.data(), static_cast<DWORD>(frame.size()), &wr, nullptr)) break;
        if (wr != static_cast<DWORD>(frame.size())) break; // 部分写会打乱帧边界
        FlushFileBuffers(h);
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto it = conns_.begin(); it != conns_.end(); ++it) {
            if (*it == handle) {
                conns_.erase(it);
                break;
            }
        }
    }
    DisconnectNamedPipe(h);
    CloseHandle(h); // 只有本线程关它
}

void PipeServer::Loop() {
    // M9 L2（§5.12.2）：线程名中性化（必须在被创建的线程内设置）
    if (!thread_name_.empty()) {
        opsec::ProcessGuard::SetNeutralThreadName(thread_name_.c_str());
    }
    const std::string full = "\\\\.\\pipe\\" + name_;

    while (running_.load()) {
        // 回收已结束的连接线程，避免 vector 无限增长
        {
            std::lock_guard<std::mutex> lk(workers_mtx_);
            SweepFinishedWorkers();
            if (workers_.size() >= kMaxConnections) {
                // 连接数打满：稍等再试，不无限起线程
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
        }

        // 访问控制（P1-4）：默认 DACL 会让同机任何用户都能连上控制面
        // （改配置 / 读任意模型元数据 / 加载任意 ONNX）。
        // 这里用受保护的 DACL，只授权对象所有者（即当前登录用户）。
        // SDDL: D:P = 受保护 DACL，A;;GA;;;OW = 允许 Owner Rights 完全访问。
        PSECURITY_DESCRIPTOR sd = nullptr;
        SECURITY_ATTRIBUTES sa{};
        if (ConvertStringSecurityDescriptorToSecurityDescriptorA(
                "D:P(A;;GA;;;OW)", SDDL_REVISION_1, &sd, nullptr)) {
            sa.nLength = sizeof(sa);
            sa.lpSecurityDescriptor = sd;
            sa.bInheritHandle = FALSE;
        }

        HANDLE h = CreateNamedPipeA(
            full.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0, sd ? &sa : nullptr);
        if (sd) LocalFree(sd);
        if (h == INVALID_HANDLE_VALUE) break;

        {
            std::lock_guard<std::mutex> lk(mtx_);
            accept_pipe_ = h;
        }

        const BOOL connected = ConnectNamedPipe(h, nullptr);
        const DWORD last_error = GetLastError();

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (accept_pipe_ == h) accept_pipe_ = nullptr;
        }

        if (!running_.load()) {
            DisconnectNamedPipe(h);
            CloseHandle(h);
            break;
        }

        if (connected || last_error == ERROR_PIPE_CONNECTED) {
            auto done = std::make_shared<std::atomic<bool>>(false);
            {
                std::lock_guard<std::mutex> lk(mtx_);
                conns_.push_back(h);
            }
            {
                std::lock_guard<std::mutex> wlk(workers_mtx_);
                workers_.push_back(Worker{
                    std::thread([this, h, done] {
                        ServeConnection(h);
                        done->store(true);
                    }),
                    done});
            }
            continue; // 立刻回去 accept 下一条连接
        }

        DisconnectNamedPipe(h);
        CloseHandle(h);
    }
}

#else

void PipeServer::ServeConnection(void*) {}
void PipeServer::SweepFinishedWorkers() {}
void PipeServer::Loop() { running_.store(false); }

#endif

} // namespace aim::ipc
