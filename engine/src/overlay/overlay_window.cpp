#include "overlay/overlay_window.h"

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aim::overlay {

namespace {

// 去标识（§5.12 配套）：窗口类名与标题都不能带用途字样。
// EnumWindows/GetClassName 一扫描就能读到它们，叫 "AimOverlay" 等于自报家门。
// 这里用中性名 + 空标题——空标题的置顶工具窗口在系统里非常常见。
const wchar_t* kClassName = L"ToolWindowHost";
const wchar_t* kWindowTitle = L"";
bool g_registered = false;

// 软件光栅化：填充三角形（预乘 alpha，写入 32bpp DIB）。
void FillTriangle(uint32_t* px, int w, int h,
                  const OverlayVertex& a, const OverlayVertex& b, const OverlayVertex& c) {
    const int minx = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int maxx = std::min(w - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int miny = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int maxy = std::min(h - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    if (minx > maxx || miny > maxy) return;

    auto edge = [](const OverlayVertex& p, const OverlayVertex& q, float x, float y) {
        return (x - p.x) * (q.y - p.y) - (y - p.y) * (q.x - p.x);
    };
    const float area = edge(a, b, c.x, c.y);
    if (std::fabs(area) < 1e-6f) return;

    const uint32_t col = a.color;
    const uint32_t al = (col >> 24) & 0xFF;
    const uint32_t r = (col >> 16) & 0xFF;
    const uint32_t g = (col >> 8) & 0xFF;
    const uint32_t bl = col & 0xFF;
    const uint32_t out = (al << 24) | ((r * al / 255) << 16) |
                         ((g * al / 255) << 8) | (bl * al / 255);

    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            const float fx = static_cast<float>(x) + 0.5f;
            const float fy = static_cast<float>(y) + 0.5f;
            const float w0 = edge(b, c, fx, fy);
            const float w1 = edge(c, a, fx, fy);
            const float w2 = edge(a, b, fx, fy);
            if ((w0 >= 0.f && w1 >= 0.f && w2 >= 0.f) ||
                (w0 <= 0.f && w1 <= 0.f && w2 <= 0.f)) {
                px[static_cast<size_t>(y) * w + x] = out;
            }
        }
    }
}

} // namespace

OverlayWindow::OverlayWindow() = default;
OverlayWindow::~OverlayWindow() { shutdown(); }

bool OverlayWindow::init(int width, int height, bool click_through, std::string* err) {
    shutdown();
    if (width <= 0 || height <= 0) {
        if (err) *err = "invalid size";
        return false;
    }

    HINSTANCE hinst = GetModuleHandleA(nullptr);
    if (!g_registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = hinst;
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) {
            if (err) *err = "RegisterClassEx failed";
            return false;
        }
        g_registered = true;
    }

    DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    if (click_through) ex |= WS_EX_TRANSPARENT;

    HWND hwnd = CreateWindowExW(ex, kClassName, kWindowTitle, WS_POPUP,
                                0, 0, width, height,
                                nullptr, nullptr, hinst, nullptr);
    if (!hwnd) {
        if (err) *err = "CreateWindowEx failed";
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        DestroyWindow(hwnd);
        if (err) *err = "CreateDIBSection failed";
        return false;
    }
    HDC mem = CreateCompatibleDC(nullptr);
    if (!mem) {
        DeleteObject(dib);
        DestroyWindow(hwnd);
        if (err) *err = "CreateCompatibleDC failed";
        return false;
    }
    SelectObject(mem, dib);

    hwnd_ = hwnd;
    dib_ = dib;
    mem_dc_ = mem;
    pixels_ = static_cast<uint32_t*>(bits);
    width_ = width;
    height_ = height;
    present_count_ = 0;
    last_vertex_count_ = 0;
    std::memset(pixels_, 0, static_cast<size_t>(width) * height * 4);
    return true;
}

void OverlayWindow::shutdown() {
    if (mem_dc_) {
        DeleteDC(static_cast<HDC>(mem_dc_));
        mem_dc_ = nullptr;
    }
    if (dib_) {
        DeleteObject(static_cast<HBITMAP>(dib_));
        dib_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
    pixels_ = nullptr;
    width_ = height_ = 0;
}

void OverlayWindow::set_visible(bool v) {
    if (hwnd_) ShowWindow(static_cast<HWND>(hwnd_), v ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void OverlayWindow::present(const PrimitiveBatch& batch) {
    if (!pixels_ || !hwnd_ || !mem_dc_) return;

    std::memset(pixels_, 0, static_cast<size_t>(width_) * height_ * 4);

    const auto& v = batch.vertices();
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
        FillTriangle(pixels_, width_, height_, v[i], v[i + 1], v[i + 2]);
    }

    HDC screen = GetDC(nullptr);
    POINT src{ 0, 0 };
    SIZE size{ width_, height_ };
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(static_cast<HWND>(hwnd_), screen, nullptr, &size,
                        static_cast<HDC>(mem_dc_), &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);

    ++present_count_;
    last_vertex_count_ = static_cast<int>(v.size());
}

} // namespace aim::overlay
