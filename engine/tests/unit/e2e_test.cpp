#include "aim_test.h"
#include "capture/dxgi_capture.h"
#include "inference/backend_ort.h"
#include "core/pipeline.h"
#include "fov/resolution_detect.h"
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aim;

#ifndef AIM_TEST_FIXTURE_DIR
#  define AIM_TEST_FIXTURE_DIR "tests/fixtures"
#endif

namespace {
struct FakeAct : spi::IActuator {
    int moves = 0;
    spi::MoveCmd last{};
    void move(const spi::MoveCmd& c) override { last = c; ++moves; }
    void click(const spi::ClickCmd&) override {}
};

bool SkipHw() {
    const char* v = std::getenv("AIM_SKIP_HW_TESTS");
    return v && std::string(v) == "1";
}
} // namespace

// M1 闭环：真实 DXGI 采集 + 真实 ORT 推理 + Pipeline（执行端用 Fake，避免移动鼠标）
AIM_TEST(e2e_dxgi_ort_pipeline) {
    if (SkipHw()) return;

    int sw = 1920, sh = 1080;
    GetPrimaryResolution(sw, sh);

    capture::DxgiCapture cap;
    spi::CaptureSpec cs;
    cs.output_monitor = 0;
    cs.roi_w = 320;
    cs.roi_h = 320;
    AIM_EXPECT_TRUE(cap.init(cs));
    if (std::string(cap.status()).find("ready") == std::string::npos) return;

    inference::OrtBackend be(inference::OrtEp::Cpu);
    spi::ModelSpec ms;
    ms.path = std::string(AIM_TEST_FIXTURE_DIR) + "/test_yolo.onnx";
    ms.imgsz = 64;
    ms.num_classes = 2;
    ms.num_kpts = 0;
    ms.conf_threshold = 0.35f;
    ms.input_nhwc_u8 = false;
    AIM_EXPECT_TRUE(be.load(ms));

    FakeAct act;
    PipelineSettings s;
    s.screen_w = static_cast<float>(sw);
    s.screen_h = static_cast<float>(sh);
    s.imgsz = 64;
    s.conf_threshold = 0.35f;
    s.iou_threshold = 0.45f;
    s.class_whitelist = {0, 1};
    s.aim = AimConfig{};
    s.scope.mode = ScopeMode::Rect;
    s.scope.half_w = static_cast<float>(sw);
    s.scope.half_h = static_cast<float>(sh);
    s.fov = Fov{};
    s.selection = TargetSelection::NearestCrosshair;
    s.armed = true;
    s.capture_spec = cs;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);

    PipelineResult r;
    for (int i = 0; i < 10 && !r.ok; ++i) r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_TRUE(r.has_target);
    AIM_EXPECT_EQ(act.moves, 1);
    if (act.moves > 0) {
        AIM_EXPECT_TRUE(std::isfinite(act.last.dx_px));
        AIM_EXPECT_TRUE(std::isfinite(act.last.dy_px));
    }
}

int main() {
    return aim::test::run_all();
}
