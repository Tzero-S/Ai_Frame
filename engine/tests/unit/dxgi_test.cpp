#include "aim_test.h"
#include "capture/dxgi_capture.h"
#include <cstdlib>
#include <string>

using namespace aim;

namespace {
bool SkipHw() {
    const char* v = std::getenv("AIM_SKIP_HW_TESTS");
    return v && std::string(v) == "1";
}
} // namespace

AIM_TEST(dxgi_capture_init_and_grab) {
    if (SkipHw()) return; // 无桌面/CI 环境显式跳过（AIM_SKIP_HW_TESTS=1）

    capture::DxgiCapture cap;
    spi::CaptureSpec spec;
    spec.output_monitor = 0;
    spec.roi_w = 320;
    spec.roi_h = 320;

    const bool ok = cap.init(spec);
    AIM_EXPECT_TRUE(ok);
    if (!ok) return;

    spi::Frame f{};
    for (int i = 0; i < 8 && !f.valid; ++i) f = cap.grab(spec);

    AIM_EXPECT_TRUE(f.valid);
    if (!f.valid) return;

    AIM_EXPECT_EQ(f.w, 320);
    AIM_EXPECT_EQ(f.h, 320);
    AIM_EXPECT_TRUE(f.bgra != nullptr);
    AIM_EXPECT_TRUE(f.seq > 0);
    AIM_EXPECT_TRUE(f.ts > 0);
}

AIM_TEST(dxgi_capture_reinit_safe) {
    if (SkipHw()) return;

    capture::DxgiCapture cap;
    spi::CaptureSpec spec;
    spec.output_monitor = 0;
    spec.roi_w = 160;
    spec.roi_h = 160;

    AIM_EXPECT_TRUE(cap.init(spec));
    AIM_EXPECT_TRUE(cap.init(spec)); // 二次 init 安全重建

    spi::Frame f{};
    for (int i = 0; i < 8 && !f.valid; ++i) f = cap.grab(spec);
    AIM_EXPECT_TRUE(f.valid);
    if (f.valid) AIM_EXPECT_EQ(f.w, 160);
}

// full_frame：采集整块输出，不再退化成"居中正方形"。
// 回归：旧实现只取 roi_w 当正方形边长，1920x1080 上 full 模式只截到 1080x1080（44% 面积）。
AIM_TEST(dxgi_full_frame_uses_whole_output) {
    if (SkipHw()) return;

    capture::DxgiCapture cap;
    spi::CaptureSpec spec;
    spec.output_monitor = 0;
    spec.roi_w = 1920;   // 模拟识别出的屏幕宽（可能不是采集输出自身的宽）
    spec.roi_h = 1080;
    spec.full_frame = true;

    AIM_EXPECT_TRUE(cap.init(spec));

    spi::Frame f{};
    for (int i = 0; i < 8 && !f.valid; ++i) f = cap.grab(spec);
    AIM_EXPECT_TRUE(f.valid);
    if (!f.valid) return;

    // 帧尺寸 == 输出尺寸，且不是正方形（除非输出本身是正方形）
    AIM_EXPECT_EQ(f.w, f.screen_w);
    AIM_EXPECT_EQ(f.h, f.screen_h);
    AIM_EXPECT_TRUE(f.w >= 1080); // 至少覆盖到原来的正方形边长
    AIM_EXPECT_TRUE(f.w * f.h > 1080 * 1080);
}

// 非正方形 ROI（roi_w != roi_h）必须按请求尺寸裁，不能被强制成正方形
AIM_TEST(dxgi_rect_roi_not_forced_square) {
    if (SkipHw()) return;

    capture::DxgiCapture cap;
    spi::CaptureSpec spec;
    spec.output_monitor = 0;
    spec.roi_w = 640;
    spec.roi_h = 360;

    AIM_EXPECT_TRUE(cap.init(spec));

    spi::Frame f{};
    for (int i = 0; i < 8 && !f.valid; ++i) f = cap.grab(spec);
    AIM_EXPECT_TRUE(f.valid);
    if (!f.valid) return;

    AIM_EXPECT_EQ(f.w, 640);
    AIM_EXPECT_EQ(f.h, 360);
}

int main() {
    return aim::test::run_all();
}
