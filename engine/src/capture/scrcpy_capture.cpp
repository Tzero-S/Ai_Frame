#include "capture/scrcpy_capture.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "capture/png_decode.h"
#include "core/clock.h"
#include "core/logger.h"

namespace aim::capture {

namespace {

std::string TempPath(const char* tag) {
    char dir[MAX_PATH]{};
    const DWORD n = GetTempPathA(MAX_PATH, dir);
    std::string name = "aim_";
    name += tag;
    name += "_";
    name += std::to_string(GetCurrentProcessId());
    name += ".tmp";
    return std::string(dir, n) + name;
}

bool RunToFile(const std::string& args, const std::string& out_path,
               int timeout_ms, std::string* err) {
    const std::string cmd =
        "cmd.exe /c adb " + args + " > \"" + out_path + "\" 2>nul";
    std::vector<char> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (err) *err = "CreateProcess failed";
        return false;
    }

    const DWORD wait = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout_ms));
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (err) *err = "adb timeout";
        return false;
    }

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (code != 0) {
        if (err) *err = "adb exit code " + std::to_string(code);
        return false;
    }
    return true;
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

std::string ReadFileText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

bool AdbAvailable(std::string* err) {
    const std::string tmp = TempPath("adbver");
    const bool ok = RunToFile("version", tmp, 3000, err);
    std::remove(tmp.c_str());
    return ok;
}

bool AdbDeviceReady(const std::string& serial, std::string* err) {
    const std::string tmp = TempPath("adbstate");
    const bool ok = RunToFile("-s " + serial + " get-state", tmp, 3000, err);
    const std::string state = ok ? ReadFileText(tmp) : std::string();
    std::remove(tmp.c_str());
    if (!ok) return false;
    if (state != "device") {
        if (err) *err = "device not ready: " + state;
        return false;
    }
    return true;
}

bool AdbScreencap(const std::string& serial, std::vector<uint8_t>& png, std::string* err) {
    const std::string tmp = TempPath("scrcap");
    const bool ok = RunToFile("-s " + serial + " exec-out screencap -p", tmp, 8000, err);
    const bool read = ok && ReadFileBytes(tmp, png);
    std::remove(tmp.c_str());
    if (!read && err && err->empty()) *err = "screencap returned empty";
    return read;
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
    std::string err;
    if (!AdbAvailable(&err)) {
        AIM_ENGINE(Error, "scrcpy: adb unavailable: " + err);
        return false;
    }
    if (!AdbDeviceReady(serial_, &err)) {
        AIM_ENGINE(Error, "scrcpy: " + err);
        return false;
    }

    edge_ = (spec.roi_w > 0) ? spec.roi_w : 640;
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
