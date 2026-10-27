#include "opsec/process_guard.h"

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>

#include <cctype>
#include <cstring>
#include <cwctype>
#include <string>
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

namespace {
// UTF-8 → UTF-16（窗口标题必须是宽字符：GetWindowTextA 会把中文等非 ANSI 字符
// 转成 '?'，导致 title_pattern 永远匹配不上，失焦即停直接失效）
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring LowerWide(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}
} // namespace

bool ProcessGuard::IsGameFocused() const {
    if (title_pattern_.empty()) return true;
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    wchar_t buf[512]{};
    GetWindowTextW(fg, buf, 512);
    const std::wstring title(buf);
    const std::wstring pattern = Utf8ToWide(title_pattern_);
    if (pattern.empty()) return true;
    return LowerWide(title).find(LowerWide(pattern)) != std::wstring::npos;
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
