#include "opsec/process_guard.h"

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>

#include <cctype>
#include <cstring>
#include <mutex>
#include <string>

namespace aim::opsec {

namespace {
std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

void ProcessGuard::Configure(bool hide_on_focus_loss, const std::string& title_pattern) {
    hide_on_focus_loss_ = hide_on_focus_loss;
    title_pattern_ = title_pattern;
}

bool ProcessGuard::TitleMatches(const std::string& title, const std::string& pattern) {
    if (pattern.empty()) return true;
    return Lower(title).find(Lower(pattern)) != std::string::npos;
}

bool ProcessGuard::IsGameFocused() const {
    if (title_pattern_.empty()) return true;
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    char buf[512]{};
    GetWindowTextA(fg, buf, sizeof(buf));
    return TitleMatches(buf, title_pattern_);
}

bool ProcessGuard::ShouldPause() const {
    return hide_on_focus_loss_ && !IsGameFocused();
}

void ProcessGuard::ApplyToolWindow(void* hwnd) {
    if (!hwnd) return;
    LONG_PTR style = GetWindowLongPtrA(static_cast<HWND>(hwnd), GWL_EXSTYLE);
    style |= WS_EX_TOOLWINDOW;
    style &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrA(static_cast<HWND>(hwnd), GWL_EXSTYLE, style);
}

void ProcessGuard::SecureZero(void* p, std::size_t n) {
    if (!p || !n) return;
    SecureZeroMemory(p, n);
}

bool ProcessGuard::SetNeutralThreadName(const char* name) {
    if (!name) return false;

    using Fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static Fn fn = nullptr;
    // 首次解析用 call_once：多线程同时首次调用时不会产生数据竞争
    static std::once_flag once;
    std::call_once(once, [] {
        FARPROC proc = GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                      "SetThreadDescription");
        if (proc) std::memcpy(&fn, &proc, sizeof(fn));
    });
    if (!fn) return false;

    const int len = static_cast<int>(std::strlen(name));
    std::wstring w(static_cast<size_t>(len), L'\0');
    for (int i = 0; i < len; ++i) {
        w[static_cast<size_t>(i)] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
    }
    return SUCCEEDED(fn(GetCurrentThread(), w.c_str()));
}

} // namespace aim::opsec
