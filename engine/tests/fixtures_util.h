#pragma once
// 测试夹具工具：模型/图片路径解析、PNG → BGRA（GDI+）。
#include <windows.h>
#include <gdiplus.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef AIM_TEST_FIXTURE_DIR
#  define AIM_TEST_FIXTURE_DIR "tests/fixtures"
#endif

namespace aim::test {

inline std::string EnvOr(const char* key, const std::string& def) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : def;
}

inline std::string FixturePath(const char* name) {
    return std::string(AIM_TEST_FIXTURE_DIR) + "/" + name;
}

// VALORANT 最优模型（README 准入：head recall 0.8223 / 9.15ms / 19MB）
inline std::string ValorantModelPath() {
    return EnvOr(
        "AIM_VALORANT_MODEL",
        "D:/MyProject/Test.Project/test/projects/VALORANT_aimbot/model/best_s960_fp16_dml.onnx");
}

inline bool FileExists(const std::string& path) {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline bool LoadPngAsBgra(const std::wstring& path, int w, int h, std::vector<uint8_t>& out) {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return false;

    bool ok = false;
    {
        Gdiplus::Bitmap bmp(path.c_str());
        if (bmp.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Bitmap dst(w, h, PixelFormat32bppARGB);
            Gdiplus::Graphics g(&dst);
            g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            g.DrawImage(&bmp, 0, 0, w, h);

            Gdiplus::BitmapData data{};
            Gdiplus::Rect rect(0, 0, w, h);
            if (dst.LockBits(&rect, Gdiplus::ImageLockModeRead,
                             PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
                out.resize(static_cast<size_t>(w) * h * 4);
                for (int y = 0; y < h; ++y) {
                    std::memcpy(out.data() + static_cast<size_t>(y) * w * 4,
                                static_cast<const uint8_t*>(data.Scan0) +
                                    static_cast<size_t>(y) * data.Stride,
                                static_cast<size_t>(w) * 4);
                }
                dst.UnlockBits(&data);
                ok = true;
            }
        }
    }
    Gdiplus::GdiplusShutdown(token);
    return ok;
}

} // namespace aim::test
