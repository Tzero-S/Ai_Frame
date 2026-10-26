#include "capture/gdi_capture.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/clock.h"
#include "core/logger.h"

namespace aim::capture {

struct GdiCapture::Impl {
    HDC screen = nullptr;
    HDC mem = nullptr;
    HBITMAP dib = nullptr;
    HBITMAP old_bmp = nullptr;
    uint32_t* bits = nullptr;
    int x = 0, y = 0, w = 0, h = 0;
    int screen_w = 0, screen_h = 0; // 虚拟屏幕尺寸（帧坐标映射用）
    std::vector<uint8_t> buffer;
    uint32_t seq = 0;
    bool ready = false;

    void Release() {
        if (mem && old_bmp) {
            SelectObject(mem, old_bmp);
            old_bmp = nullptr;
        }
        if (dib) {
            DeleteObject(dib);
            dib = nullptr;
        }
        if (mem) {
            DeleteDC(mem);
            mem = nullptr;
        }
        if (screen) {
            ReleaseDC(nullptr, screen);
            screen = nullptr;
        }
        bits = nullptr;
        buffer.clear();
        ready = false;
    }
};

GdiCapture::GdiCapture() : impl_(std::make_unique<Impl>()) {}
GdiCapture::~GdiCapture() { impl_->Release(); }

bool GdiCapture::init(const spi::CaptureSpec& spec) {
    impl_->Release();

    impl_->screen = GetDC(nullptr);
    if (!impl_->screen) {
        AIM_ENGINE(Error, "GDI GetDC failed");
        return false;
    }

    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) {
        impl_->Release();
        AIM_ENGINE(Error, "GDI virtual screen size invalid");
        return false;
    }

    // full_frame：整块虚拟桌面；否则按请求的 roi_w x roi_h 居中裁剪
    //（旧实现只取 roi_w 当正方形边长，full 模式会退化成居中正方形）
    int rw = spec.full_frame ? vw : ((spec.roi_w > 0) ? spec.roi_w : 640);
    int rh = spec.full_frame ? vh : ((spec.roi_h > 0) ? spec.roi_h : rw);
    rw = std::min(rw, vw);
    rh = std::min(rh, vh);

    impl_->w = rw;
    impl_->h = rh;
    impl_->x = vx + (vw - rw) / 2;
    impl_->y = vy + (vh - rh) / 2;
    impl_->screen_w = vw;
    impl_->screen_h = vh;

    impl_->mem = CreateCompatibleDC(impl_->screen);
    if (!impl_->mem) {
        impl_->Release();
        AIM_ENGINE(Error, "GDI CreateCompatibleDC failed");
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = rw;
    bmi.bmiHeader.biHeight = -rh; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    impl_->dib = CreateDIBSection(impl_->screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!impl_->dib || !bits) {
        impl_->Release();
        AIM_ENGINE(Error, "GDI CreateDIBSection failed");
        return false;
    }
    impl_->old_bmp = static_cast<HBITMAP>(SelectObject(impl_->mem, impl_->dib));
    impl_->bits = static_cast<uint32_t*>(bits);
    impl_->buffer.resize(static_cast<size_t>(rw) * rh * 4);
    impl_->ready = true;
    return true;
}

spi::Frame GdiCapture::grab(const spi::CaptureSpec&) {
    if (!impl_->ready) return spi::Frame::empty();

    // 不使用 CAPTUREBLT：避免把覆盖层自身也采进来
    if (!BitBlt(impl_->mem, 0, 0, impl_->w, impl_->h,
                impl_->screen, impl_->x, impl_->y, SRCCOPY)) {
        return spi::Frame::empty();
    }
    GdiFlush();

    std::memcpy(impl_->buffer.data(), impl_->bits,
                static_cast<size_t>(impl_->w) * impl_->h * 4);

    spi::Frame f;
    f.seq = static_cast<int>(++impl_->seq);
    f.ts = static_cast<uint64_t>(Clock::NowUs());
    f.w = impl_->w;
    f.h = impl_->h;
    f.bgra = impl_->buffer.data();
    f.valid = true;
    f.screen_w = impl_->screen_w;
    f.screen_h = impl_->screen_h;
    f.src_x = impl_->x;
    f.src_y = impl_->y;
    f.src_w = impl_->w;
    f.src_h = impl_->h;
    return f;
}

const char* GdiCapture::status() const {
    return impl_->ready ? "gdi: ready" : "gdi: not initialized";
}

} // namespace aim::capture
