#include "aim_test.h"
#include "capture/gdi_capture.h"
#include <cstdlib>
#include <string>

using namespace aim;

namespace {
bool SkipHw() {
    const char* v = std::getenv("AIM_SKIP_HW_TESTS");
    return v && std::string(v) == "1";
}
} // namespace

AIM_TEST(gdi_capture_init_and_grab) {
    if (SkipHw()) return;

    capture::GdiCapture cap;
    spi::CaptureSpec spec;
    spec.roi_w = 320;
    spec.roi_h = 320;

    AIM_EXPECT_TRUE(cap.init(spec));
    const spi::Frame f = cap.grab(spec);

    AIM_EXPECT_TRUE(f.valid);
    if (f.valid) {
        AIM_EXPECT_EQ(f.w, 320);
        AIM_EXPECT_EQ(f.h, 320);
        AIM_EXPECT_TRUE(f.bgra != nullptr);
        AIM_EXPECT_TRUE(f.seq > 0);
    }
}

AIM_TEST(gdi_capture_reinit_safe) {
    if (SkipHw()) return;

    capture::GdiCapture cap;
    spi::CaptureSpec spec;
    spec.roi_w = 160;
    spec.roi_h = 160;

    AIM_EXPECT_TRUE(cap.init(spec));
    AIM_EXPECT_TRUE(cap.init(spec)); // 二次 init 安全重建

    const spi::Frame f = cap.grab(spec);
    AIM_EXPECT_TRUE(f.valid);
    if (f.valid) AIM_EXPECT_EQ(f.w, 160);
}

int main() {
    return aim::test::run_all();
}
