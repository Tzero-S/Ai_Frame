#include "aim_test.h"
#include "fixtures_util.h"
#include "capture/png_decode.h"
#include "capture/scrcpy_capture.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace aim;

namespace {
bool ReadAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}
} // namespace

AIM_TEST(png_decode_fixture) {
    std::vector<uint8_t> png;
    if (!ReadAll(test::FixturePath("valorant_sample_960.png"), png)) return;

    std::vector<uint8_t> bgra;
    std::string err;
    AIM_EXPECT_TRUE(capture::DecodePngToBgra(png.data(), png.size(), 320, 320, bgra, &err));
    AIM_EXPECT_EQ(bgra.size(), static_cast<size_t>(320) * 320 * 4);

    int nonzero = 0;
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
        if (bgra[i] || bgra[i + 1] || bgra[i + 2]) ++nonzero;
    }
    AIM_EXPECT_TRUE(nonzero > 0);
}

AIM_TEST(png_decode_invalid) {
    const uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> out;
    std::string err;
    AIM_EXPECT_FALSE(capture::DecodePngToBgra(junk, sizeof(junk), 64, 64, out, &err));
    AIM_EXPECT_TRUE(!err.empty());
}

AIM_TEST(scrcpy_requires_serial) {
    capture::ScrcpyCapture cap;
    spi::CaptureSpec spec;
    spec.roi_w = 320;
    spec.roi_h = 320;
    AIM_EXPECT_FALSE(cap.init(spec)); // 未 configure → serial 为空
}

AIM_TEST(scrcpy_bogus_serial_fails) {
    capture::ScrcpyCapture cap;
    cap.configure("AIM_NO_SUCH_DEVICE_999", "127.0.0.1", 27183);

    spi::CaptureSpec spec;
    spec.roi_w = 320;
    spec.roi_h = 320;
    AIM_EXPECT_FALSE(cap.init(spec)); // adb 缺失或设备不存在
    AIM_EXPECT_TRUE(std::string(cap.status()).find("not initialized") != std::string::npos);
}

int main() {
    return aim::test::run_all();
}
