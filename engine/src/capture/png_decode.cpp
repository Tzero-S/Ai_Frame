#include "capture/png_decode.h"

#include <windows.h>
#include <gdiplus.h>
#include <objidl.h>

#include <cstring>

namespace aim::capture {

bool DecodePngToBgra(const uint8_t* data, size_t len,
                     int out_w, int out_h,
                     std::vector<uint8_t>& out,
                     std::string* err) {
    if (!data || len == 0 || out_w <= 0 || out_h <= 0) {
        if (err) *err = "invalid png input";
        return false;
    }

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!mem) {
        if (err) *err = "GlobalAlloc failed";
        return false;
    }
    void* p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        if (err) *err = "GlobalLock failed";
        return false;
    }
    std::memcpy(p, data, len);
    GlobalUnlock(mem);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream)) || !stream) {
        GlobalFree(mem);
        if (err) *err = "CreateStreamOnHGlobal failed";
        return false;
    }

    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        stream->Release();
        if (err) *err = "GdiplusStartup failed";
        return false;
    }

    bool ok = false;
    {
        Gdiplus::Bitmap src(stream);
        if (src.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Bitmap dst(out_w, out_h, PixelFormat32bppARGB);
            Gdiplus::Graphics g(&dst);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
            g.DrawImage(&src, 0, 0, out_w, out_h);

            Gdiplus::BitmapData bd{};
            Gdiplus::Rect rect(0, 0, out_w, out_h);
            if (dst.LockBits(&rect, Gdiplus::ImageLockModeRead,
                             PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
                out.resize(static_cast<size_t>(out_w) * out_h * 4);
                for (int y = 0; y < out_h; ++y) {
                    std::memcpy(out.data() + static_cast<size_t>(y) * out_w * 4,
                                static_cast<const uint8_t*>(bd.Scan0) +
                                    static_cast<size_t>(y) * bd.Stride,
                                static_cast<size_t>(out_w) * 4);
                }
                dst.UnlockBits(&bd);
                ok = true;
            }
        } else if (err) {
            *err = "GDI+ decode failed (not a valid PNG?)";
        }
    }

    Gdiplus::GdiplusShutdown(token);
    stream->Release();
    return ok;
}

} // namespace aim::capture
