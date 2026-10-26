#include "aim_test.h"
#include "fixtures_util.h"
#include "capture/dxgi_capture.h"
#include "inference/backend_ort.h"
#include "core/pipeline.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace aim;

namespace {

struct FixtureCapture : spi::ICapture {
    const uint8_t* bgra = nullptr;
    int w = 0, h = 0, seq = 0;

    bool init(const spi::CaptureSpec&) override { return true; }
    spi::Frame grab(const spi::CaptureSpec&) override {
        spi::Frame f;
        f.valid = (bgra != nullptr);
        f.w = w;
        f.h = h;
        f.bgra = bgra;
        f.seq = ++seq;
        f.ts = 1;
        return f;
    }
};

struct FakeAct : spi::IActuator {
    int moves = 0;
    spi::MoveCmd last{};
    void move(const spi::MoveCmd& c) override { last = c; ++moves; }
    void click(const spi::ClickCmd&) override {}
};

spi::ModelSpec RealModelSpec(const std::string& path) {
    spi::ModelSpec ms;
    ms.path = path;
    ms.imgsz = 960;
    ms.num_classes = 2;
    ms.num_kpts = 0;
    ms.conf_threshold = 0.35f;
    ms.input_nhwc_u8 = false;
    ms.threads = 4;
    return ms;
}

} // namespace

// 真实模型 + 固定验证图 + 完整 Pipeline（选择/瞄准/执行端 Fake）
AIM_TEST(e2e_real_model_fixture_pipeline) {
    const std::string model = test::ValorantModelPath();
    const std::string image = test::FixturePath("valorant_sample_960.png");
    if (!test::FileExists(model) || !test::FileExists(image)) return;

    std::vector<uint8_t> bgra;
    if (!test::LoadPngAsBgra(std::filesystem::path(image).wstring(), 960, 960, bgra)) return;

    FixtureCapture cap;
    cap.bgra = bgra.data();
    cap.w = 960;
    cap.h = 960;

    inference::OrtBackend be(inference::OrtEp::Cpu);
    AIM_EXPECT_TRUE(be.load(RealModelSpec(model)));

    FakeAct act;
    PipelineSettings s;
    s.screen_w = 960.f;
    s.screen_h = 960.f;
    s.imgsz = 960;
    s.conf_threshold = 0.35f;
    s.iou_threshold = 0.45f;
    s.class_whitelist = {0, 1};
    s.aim = AimConfig{};
    s.scope.mode = ScopeMode::Rect;
    s.scope.half_w = 960.f;
    s.scope.half_h = 960.f;
    s.fov = Fov{};
    s.selection = TargetSelection::NearestCrosshair;
    s.armed = true;
    s.capture_spec.roi_w = 960;
    s.capture_spec.roi_h = 960;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);
    const auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_EQ(r.detection_count, 2u);
    AIM_EXPECT_TRUE(r.has_target);
    AIM_EXPECT_EQ(act.moves, 1);
    if (act.moves > 0) {
        // 最近目标是 Body；head 锚点 = (469.245, 447.83)，屏幕中心 (480,480)
        AIM_EXPECT_NEAR(act.last.dx_px, -10.755f, 3.f);
        AIM_EXPECT_NEAR(act.last.dy_px, -32.17f, 3.f);
    }
}

// 真实 DXGI 帧 + 真实模型推理冒烟（桌面无目标时允许 0 检测）
AIM_TEST(e2e_dxgi_real_model_smoke) {
    const std::string model = test::ValorantModelPath();
    if (!test::FileExists(model)) return;

    const char* skip = std::getenv("AIM_SKIP_HW_TESTS");
    if (skip && std::string(skip) == "1") return;

    capture::DxgiCapture cap;
    spi::CaptureSpec cs;
    cs.output_monitor = 0;
    cs.roi_w = 960;
    cs.roi_h = 960;
    AIM_EXPECT_TRUE(cap.init(cs));
    if (std::string(cap.status()).find("ready") == std::string::npos) return;

    spi::Frame f{};
    for (int i = 0; i < 8 && !f.valid; ++i) f = cap.grab(cs);
    AIM_EXPECT_TRUE(f.valid);
    if (!f.valid) return;

    inference::OrtBackend be(inference::OrtEp::Cpu);
    AIM_EXPECT_TRUE(be.load(RealModelSpec(model)));

    std::vector<Detection> dets;
    AIM_EXPECT_TRUE(be.infer(f, dets));
    AIM_EXPECT_TRUE(be.counters().infer_ms > 0.f);
}

int main() {
    return aim::test::run_all();
}
