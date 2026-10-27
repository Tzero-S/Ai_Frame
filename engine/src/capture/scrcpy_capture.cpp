#include "capture/scrcpy_capture.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "capture/png_decode.h"
#include "core/clock.h"
#include "core/logger.h"

// 安全要点（P0-3）：这里**不再经过 shell**。
// 旧实现拼 "cmd.exe /c adb -s <serial> ..."，serial 来自配置且未转义，
// 形如 `x & calc.exe` 即执行任意命令。现在：
//   ① serial 先过白名单（adb 序列号的实际字符集）；
//   ② 直接 CreateProcess 启动 adb.exe，命令行按 Windows 规则加引号，不经 cmd 解析；
//   ③ stdout 走匿名管道读进内存，不再落临时文件（顺带去掉可预测的 %TEMP% 中间文件）。

namespace aim::capture {

// adb 序列号允许的字符：字母数字 + . _ : -（如 emulator-5554 / 192.168.1.7:5555 / R58M123ABC）
bool IsSafeAdbSerial(const std::string& s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == ':' || c == '-';
        if (!ok) return false;
    }
    return true;
}

namespace {

// Windows 命令行参数转义：含空格/引号时用双引号包裹，内部引号按反斜杠规则转义。
std::string QuoteArg(const std::string& s) {
    if (s.find_first_of(" \t\"") == std::string::npos) return s;
    std::string out = "\"";
    for (size_t i = 0; i < s.size(); ++i) {
        size_t backslashes = 0;
        while (i < s.size() && s[i] == '\\') { ++backslashes; ++i; }
        if (i == s.size()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (s[i] == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(s[i]);
        }
    }
    out.push_back('"');
    return out;
}

std::string AdbExecutable() {
    const char* env = std::getenv("AIM_ADB");
    return (env && *env) ? std::string(env) : std::string("adb.exe");
}

// 起一个子进程并把 stdout 收进 out（二进制安全）。返回进程退出码；-1 表示启动失败/超时。
int RunProcessCapture(const std::string& cmdline, int timeout_ms,
                      std::vector<uint8_t>& out, std::string* err) {
    out.clear();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        if (err) *err = "CreatePipe failed";
        return -1;
    }
    // 读端不继承，避免子进程持有它导致读不到 EOF
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, nullptr);

    // 只把需要的两个句柄传给子进程（PROC_THREAD_ATTRIBUTE_HANDLE_LIST）。
    // 直接用 bInheritHandles=TRUE 会把父进程所有可继承句柄都塞给 adb ——
    // 包括调用方的 stdout 管道。adb 会 fork 常驻 server，它一旦持有该写端，
    // 上层（ctest / IDE）就永远等不到 EOF，表现为"测试卡死到超时"。
    STARTUPINFOEXA six{};
    six.StartupInfo.cb = sizeof(six);
    six.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    six.StartupInfo.wShowWindow = SW_HIDE;
    six.StartupInfo.hStdOutput = write_pipe;
    six.StartupInfo.hStdError = (nul != INVALID_HANDLE_VALUE) ? nul : write_pipe;
    six.StartupInfo.hStdInput = nullptr;

    HANDLE inherit_list[2] = {write_pipe, six.StartupInfo.hStdError};
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<uint8_t> attr_buf(attr_size);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
    bool attrs_ok = InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) != FALSE;
    if (attrs_ok) {
        attrs_ok = UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                            inherit_list, sizeof(inherit_list), nullptr,
                                            nullptr) != FALSE;
    }
    six.lpAttributeList = attrs_ok ? attrs : nullptr;
    const DWORD flags = CREATE_NO_WINDOW | (attrs_ok ? EXTENDED_STARTUPINFO_PRESENT : 0);

    std::vector<char> buf(cmdline.begin(), cmdline.end());
    buf.push_back('\0');

    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE, flags,
                                        nullptr, nullptr, &six.StartupInfo, &pi);
    if (attrs_ok) DeleteProcThreadAttributeList(attrs);
    CloseHandle(write_pipe); // 父进程不再需要写端
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);

    if (!started) {
        CloseHandle(read_pipe);
        if (err) *err = "CreateProcess failed (" + std::to_string(GetLastError()) + ")";
        return -1;
    }

    const DWORD deadline = GetTickCount() + static_cast<DWORD>(timeout_ms);
    bool timed_out = false;
    uint8_t chunk[16384];
    while (true) {
        DWORD avail = 0;
        if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail, nullptr)) break; // 子进程退出
        if (avail > 0) {
            DWORD got = 0;
            const DWORD want = std::min<DWORD>(avail, static_cast<DWORD>(sizeof(chunk)));
            if (!ReadFile(read_pipe, chunk, want, &got, nullptr) || got == 0) break;
            out.insert(out.end(), chunk, chunk + got);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) {
            // 进程已退出，把管道里剩下的读完
            while (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD got = 0;
                const DWORD want = std::min<DWORD>(avail, static_cast<DWORD>(sizeof(chunk)));
                if (!ReadFile(read_pipe, chunk, want, &got, nullptr) || got == 0) break;
                out.insert(out.end(), chunk, chunk + got);
            }
            break;
        }
        if (GetTickCount() > deadline) { timed_out = true; break; }
    }

    CloseHandle(read_pipe);
    if (timed_out) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (err) *err = "adb timeout";
        return -1;
    }

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

std::string TrimTrailing(const std::vector<uint8_t>& data) {
    size_t n = data.size();
    while (n > 0 && (data[n - 1] == '\n' || data[n - 1] == '\r' || data[n - 1] == ' ')) --n;
    return std::string(reinterpret_cast<const char*>(data.data()), n);
}

bool RunAdb(const std::string& serial, const std::string& args, int timeout_ms,
            std::vector<uint8_t>& out, std::string* err) {
    if (!IsSafeAdbSerial(serial)) {
        if (err) *err = "serial 含非法字符（只允许字母数字与 . _ : -）";
        return false;
    }
    const std::string cmdline =
        QuoteArg(AdbExecutable()) + " -s " + QuoteArg(serial) + " " + args;
    const int code = RunProcessCapture(cmdline, timeout_ms, out, err);
    if (code != 0) {
        if (err && err->empty()) *err = "adb exit code " + std::to_string(code);
        return false;
    }
    return true;
}

bool AdbAvailable(std::string* err) {
    std::vector<uint8_t> out;
    const std::string cmdline = QuoteArg(AdbExecutable()) + " version";
    const int code = RunProcessCapture(cmdline, 3000, out, err);
    if (code != 0) {
        if (err && err->empty()) *err = "adb not available";
        return false;
    }
    return true;
}

bool AdbDeviceReady(const std::string& serial, std::string* err) {
    std::vector<uint8_t> out;
    if (!RunAdb(serial, "get-state", 3000, out, err)) return false;
    const std::string state = TrimTrailing(out);
    if (state != "device") {
        if (err) *err = "device not ready: " + state;
        return false;
    }
    return true;
}

bool AdbScreencap(const std::string& serial, std::vector<uint8_t>& png, std::string* err) {
    if (!RunAdb(serial, "exec-out screencap -p", 8000, png, err)) return false;
    if (png.empty()) {
        if (err) *err = "screencap returned empty";
        return false;
    }
    return true;
}

} // namespace

ScrcpyCapture::ScrcpyCapture() = default;
ScrcpyCapture::~ScrcpyCapture() = default;

bool ScrcpyCapture::configure(const CaptureConfig& cfg) {
    configure(cfg.scrcpy.serial, cfg.scrcpy.host, cfg.scrcpy.port);
    return true;
}

void ScrcpyCapture::configure(const std::string& serial, const std::string& host, int port) {
    serial_ = serial;
    host_ = host;
    port_ = port;
}

bool ScrcpyCapture::init(const spi::CaptureSpec& spec) {
    ready_ = false;
    if (serial_.empty()) {
        AIM_ENGINE(Error, "scrcpy: serial required");
        return false;
    }
    if (!IsSafeAdbSerial(serial_)) {
        // 明确报错而不是"执行失败"：非法字符几乎一定来自配置手误或注入尝试
        AIM_ENGINE(Error, "scrcpy: serial 含非法字符，拒绝执行（只允许字母数字与 . _ : -）");
        return false;
    }
    std::string err;
    if (!AdbAvailable(&err)) {
        AIM_ENGINE(Error, "scrcpy: adb unavailable: " + err);
        return false;
    }
    if (!AdbDeviceReady(serial_, &err)) {
        AIM_ENGINE(Error, "scrcpy: " + err);
        return false;
    }

    // ROI 边长夹取（P2-3）：配置允许到 20000，但 edge²×4 会吃掉 1.6 GB，这里限制在合理上限
    int e = (spec.roi_w > 0) ? spec.roi_w : 640;
    e = std::min(e, 2048);
    e = std::max(e, 32);
    edge_ = e;
    ready_ = true;
    return true;
}

spi::Frame ScrcpyCapture::grab(const spi::CaptureSpec&) {
    if (!ready_) return spi::Frame::empty();

    std::vector<uint8_t> png;
    std::string err;
    if (!AdbScreencap(serial_, png, &err)) {
        AIM_ENGINE(Warn, "scrcpy grab failed: " + err);
        return spi::Frame::empty();
    }

    if (!DecodePngToBgra(png.data(), png.size(), edge_, edge_, buffer_, &err)) {
        AIM_ENGINE(Warn, "scrcpy decode failed: " + err);
        return spi::Frame::empty();
    }

    spi::Frame f;
    f.seq = static_cast<int>(++seq_);
    f.ts = static_cast<uint64_t>(Clock::NowUs());
    f.w = edge_;
    f.h = edge_;
    f.bgra = buffer_.data();
    f.valid = true;
    // 投屏路径把整块设备画面重采样到 edge×edge，等价于"帧即整屏"。
    f.screen_w = edge_;
    f.screen_h = edge_;
    f.src_w = edge_;
    f.src_h = edge_;
    return f;
}

const char* ScrcpyCapture::status() const {
    return ready_ ? "scrcpy: ready (adb screencap fallback)" : "scrcpy: not initialized";
}

} // namespace aim::capture
