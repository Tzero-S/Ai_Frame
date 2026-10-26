#include "aim_test.h"
#include <cstdio>
#include "core/pipeline.h"
#include "core/spi.h"
#include <vector>

using namespace aim;

namespace {

struct FakeCapture : spi::ICapture {
    bool valid = true;
    // ROI 信息（默认 0 = 帧即整屏，兼容既有用例）
    int screen_w = 0, screen_h = 0;
    int src_x = 0, src_y = 0, src_w = 0, src_h = 0;
    int w = 640, h = 640;
    bool init(const spi::CaptureSpec&) override { return true; }
    spi::Frame grab(const spi::CaptureSpec&) override {
        spi::Frame f;
        f.valid = valid;
        f.w = w; f.h = h; f.seq = 1; f.ts = 1;
        static uint8_t px[4] = {0, 0, 0, 255};
        f.bgra = px;
        f.screen_w = screen_w;
        f.screen_h = screen_h;
        f.src_x = src_x;
        f.src_y = src_y;
        f.src_w = src_w;
        f.src_h = src_h;
        return f;
    }
};

struct FakeBackend : spi::IBackend {
    std::vector<Detection> dets;
    bool load(const spi::ModelSpec&) override { return true; }
    bool infer(const spi::Frame&, std::vector<Detection>& out) override { out = dets; return true; }
    std::string backend_name() const override { return "fake"; }
    spi::PerfCounters counters() const override { return {}; }
};

struct FakeActuator : spi::IActuator {
    spi::MoveCmd last{};
    int moves = 0;
    void move(const spi::MoveCmd& c) override { last = c; ++moves; }
    void click(const spi::ClickCmd&) override {}
};

struct FakeOverlay : spi::IOverlay {
    int draws = 0;
    int shows = 0;
    int hides = 0;
    size_t last_count = 0;
    RectF last_box{};
    void draw(const spi::Frame&, const std::vector<Detection>& d) override {
        ++draws;
        last_count = d.size();
        if (!d.empty()) last_box = d[0].box;
    }
    void set_visible(bool v) override {
        if (v) ++shows; else ++hides;
    }
};

Detection MakeModelDet(float x, float y, float w, float h, float score, int cls = 0) {
    Detection d;
    d.box = RectF{x, y, w, h};
    d.score = score;
    d.class_id = cls;
    return d;
}

PipelineSettings BaseSettings() {
    PipelineSettings s;
    s.screen_w = 1920.f; s.screen_h = 1080.f; s.imgsz = 640;
    s.conf_threshold = 0.35f; s.iou_threshold = 0.45f;
    s.class_whitelist = {0};
    s.aim = AimConfig{};
    // 本套件验证几何/时序，平滑单独成例（否则每帧位移被滤波，断言失真）
    s.aim.smoothing.enabled = false;
    s.scope.mode = ScopeMode::Rect; s.scope.half_w = 320.f; s.scope.half_h = 240.f;
    s.fov = Fov{};
    s.selection = TargetSelection::NearestCrosshair;
    s.armed = true;
    return s;
}

} // namespace

AIM_TEST(pipeline_end_to_end_move) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    // 模型空间框 (300,300,40,80) → 屏幕 (900,506.25,120,135)，头部锚点 y=522.45
    be.dets.push_back(MakeModelDet(300, 300, 40, 80, 0.9f, 0));

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, BaseSettings());
    auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_TRUE(r.has_target);
    AIM_EXPECT_EQ(r.detection_count, 1u);
    AIM_EXPECT_EQ(act.moves, 1);
    AIM_EXPECT_NEAR(act.last.dx_px, 0.f, 1e-3f);
    AIM_EXPECT_NEAR(act.last.dy_px, -17.55f, 0.05f); // 522.45 - 540
    AIM_EXPECT_EQ(act.last.seq, 1u);
}

// ROI 感知映射（§5.2.2 / §5.4）：采集为屏幕中心 960×960 裁剪（原点 800,320），
// 模型输入 960×960。模型空间框 (460,460,40,80) 应映射到屏幕 (1260,780,40,80)，
// 而非"按整屏 2560×1600 拉伸"得到的 (1226.67,766.67)。
AIM_TEST(pipeline_roi_aware_mapping) {
    FakeCapture cap;
    cap.w = 960; cap.h = 960;
    cap.screen_w = 2560; cap.screen_h = 1600;
    cap.src_x = 800; cap.src_y = 320; cap.src_w = 960; cap.src_h = 960;

    FakeBackend be;
    FakeActuator act;
    be.dets.push_back(MakeModelDet(460, 460, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings();
    s.screen_w = 2560.f; s.screen_h = 1600.f;
    s.imgsz = 960;
    s.scope.mode = ScopeMode::Rect; s.scope.half_w = 1280.f; s.scope.half_h = 800.f;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);
    auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_TRUE(r.has_target);
    AIM_EXPECT_EQ(r.detection_count, 1u);
    AIM_EXPECT_EQ(act.moves, 1);
    // 框中心 x = 1260+20 = 1280 == 屏幕中心 → dx = 0
    AIM_EXPECT_NEAR(act.last.dx_px, 0.f, 0.05f);
    // 头部锚点 y = 780 + 80*0.12 = 789.6；屏幕中心 800 → dy = -10.4
    AIM_EXPECT_NEAR(act.last.dy_px, -10.4f, 0.05f);
}

// 覆盖层接线：Pipeline 在完成坐标映射后把"屏幕坐标"检测框推给 IOverlay
AIM_TEST(pipeline_overlay_receives_screen_coords) {
    FakeCapture cap;
    cap.w = 960; cap.h = 960;
    cap.screen_w = 2560; cap.screen_h = 1600;
    cap.src_x = 800; cap.src_y = 320; cap.src_w = 960; cap.src_h = 960;

    FakeBackend be;
    FakeActuator act;
    FakeOverlay ov;
    be.dets.push_back(MakeModelDet(460, 460, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings();
    s.screen_w = 2560.f; s.screen_h = 1600.f;
    s.imgsz = 960;
    s.scope.mode = ScopeMode::Rect; s.scope.half_w = 1280.f; s.scope.half_h = 800.f;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act, &ov}, s);
    const auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_EQ(ov.draws, 1);
    AIM_EXPECT_EQ(ov.last_count, 1u);
    // 框左上角应为屏幕坐标 (1260,780)，而不是模型坐标 (460,460)
    AIM_EXPECT_NEAR(ov.last_box.x, 1260.f, 0.05f);
    AIM_EXPECT_NEAR(ov.last_box.y, 780.f, 0.05f);
}

// M9 L2 失焦即停：标题永不匹配 → 隐藏覆盖层、不推理、不下发
AIM_TEST(pipeline_focus_pause_hides_overlay) {
    FakeCapture cap; FakeBackend be; FakeActuator act; FakeOverlay ov;
    be.dets.push_back(MakeModelDet(300, 300, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings();
    s.focus_pause = true;
    s.game_title = "__aim_no_such_window__";

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act, &ov}, s);
    const auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);        // 正常空闲帧，不是故障
    AIM_EXPECT_FALSE(r.has_target);
    AIM_EXPECT_EQ(act.moves, 0);
    AIM_EXPECT_EQ(ov.draws, 0);
    AIM_EXPECT_EQ(ov.hides, 1);
}

// 未开启失焦暂停时不得隐藏覆盖层（默认行为保持不变）
AIM_TEST(pipeline_focus_pause_disabled_draws) {
    FakeCapture cap; FakeBackend be; FakeActuator act; FakeOverlay ov;
    be.dets.push_back(MakeModelDet(300, 300, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings();
    s.focus_pause = false;
    s.game_title = "__aim_no_such_window__";

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act, &ov}, s);
    const auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_EQ(ov.draws, 1);
    AIM_EXPECT_EQ(ov.hides, 0);
}

AIM_TEST(pipeline_disarmed_no_move) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    be.dets.push_back(MakeModelDet(300, 300, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings();
    s.armed = false;
    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);
    auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_FALSE(r.has_target);
    AIM_EXPECT_EQ(act.moves, 0);
}

AIM_TEST(pipeline_out_of_scope_no_move) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    be.dets.push_back(MakeModelDet(600, 300, 40, 80, 0.9f, 0)); // 屏幕 x 中心 1860 → dx=900

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, BaseSettings());
    auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_FALSE(r.has_target);
    AIM_EXPECT_EQ(act.moves, 0);
}

AIM_TEST(pipeline_class_filter_blocks) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    be.dets.push_back(MakeModelDet(300, 300, 40, 80, 0.9f, 1)); // 类别 1，白名单仅 0

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, BaseSettings());
    auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_EQ(r.detection_count, 0u);
    AIM_EXPECT_FALSE(r.has_target);
    AIM_EXPECT_EQ(act.moves, 0);
}

AIM_TEST(pipeline_nms_collapses_duplicates) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    be.dets.push_back(MakeModelDet(300, 300, 40, 80, 0.9f, 0));
    be.dets.push_back(MakeModelDet(302, 301, 40, 80, 0.8f, 0)); // 高重叠

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, BaseSettings());
    auto r = p.ProcessFrame();
    AIM_EXPECT_EQ(r.detection_count, 1u); // NMS 后仅 1
    AIM_EXPECT_EQ(act.moves, 1);
}

AIM_TEST(pipeline_invalid_frame) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    cap.valid = false;
    be.dets.push_back(MakeModelDet(300, 300, 40, 80, 0.9f, 0));

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, BaseSettings());
    auto r = p.ProcessFrame();

    AIM_EXPECT_FALSE(r.ok);
    AIM_EXPECT_EQ(act.moves, 0);
}

AIM_TEST(pipeline_lead_extrapolation) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    PipelineSettings s = BaseSettings();
    s.aim.latency_compensation_ms = 100;
    s.aim.smoothing.lead = 1.0f;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);

    // 目标每帧右移 10 模型像素 → 屏幕 30px/100ms
    be.dets = { MakeModelDet(300, 300, 40, 80, 0.9f, 0) };
    AIM_EXPECT_TRUE(p.ProcessFrame(0).ok);
    be.dets = { MakeModelDet(310, 300, 40, 80, 0.9f, 0) };
    AIM_EXPECT_TRUE(p.ProcessFrame(100).ok);
    be.dets = { MakeModelDet(320, 300, 40, 80, 0.9f, 0) };
    AIM_EXPECT_TRUE(p.ProcessFrame(200).ok);

    // 第三帧：anchor x=1020，v=0.3px/ms，lead 100ms → +30 → 1050；delta=90
    AIM_EXPECT_EQ(act.moves, 3);
    AIM_EXPECT_NEAR(act.last.dx_px, 90.f, 0.5f);
    AIM_EXPECT_NEAR(act.last.dy_px, -17.55f, 0.1f);
}

AIM_TEST(pipeline_lead_disabled) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    PipelineSettings s = BaseSettings();
    s.aim.latency_compensation_ms = 100;
    s.aim.smoothing.lead = 0.0f;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);
    be.dets = { MakeModelDet(300, 300, 40, 80, 0.9f, 0) }; p.ProcessFrame(0);
    be.dets = { MakeModelDet(310, 300, 40, 80, 0.9f, 0) }; p.ProcessFrame(100);
    be.dets = { MakeModelDet(320, 300, 40, 80, 0.9f, 0) }; p.ProcessFrame(200);

    AIM_EXPECT_NEAR(act.last.dx_px, 60.f, 0.5f); // 1020 - 960
}

AIM_TEST(pipeline_lead_resets_on_target_loss) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    PipelineSettings s = BaseSettings();
    s.aim.latency_compensation_ms = 100;
    s.aim.smoothing.lead = 1.0f;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);

    be.dets = { MakeModelDet(300, 300, 40, 80, 0.9f, 0) }; p.ProcessFrame(0);
    be.dets = { MakeModelDet(310, 300, 40, 80, 0.9f, 0) }; p.ProcessFrame(100);
    be.dets.clear(); p.ProcessFrame(200); // 丢失 → 重置外推

    be.dets = { MakeModelDet(320, 300, 40, 80, 0.9f, 0) };
    p.ProcessFrame(300);
    AIM_EXPECT_NEAR(act.last.dx_px, 60.f, 0.5f); // 重置后仅 1 样本
}

AIM_TEST(pipeline_behavior_shaping_bounds) {
    FakeCapture cap;
    FakeBackend be;
    FakeActuator act;
    be.dets.push_back(MakeModelDet(300, 300, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings();
    s.behavior_shaping = true;
    s.anti_detect_seed = 42;
    s.anti_detect.level = AntiDetectLevel::Standard;
    s.anti_detect.jitter_px = 1.0f;
    s.anti_detect.timing_jitter_ratio = 0.1f;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);
    const auto r = p.ProcessFrame(0);

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_EQ(act.moves, 1);
    // 基准 (0, -17.55)，抖动 ≤ 3σ = 3px
    AIM_EXPECT_NEAR(act.last.dx_px, 0.f, 3.01f);
    AIM_EXPECT_NEAR(act.last.dy_px, -17.55f, 3.01f);
}

int main() {
    return aim::test::run_all();
}

// 平滑接通验证：目标跳变后，下发的位移滞后于原始几何值，随后逐步收敛。
// 注意：r.delta_px 是几何瞄准量（遥测用），实际下发值看 actuator。
AIM_TEST(pipeline_smoothing_lags_then_converges) {
    FakeCapture cap; FakeBackend be; FakeActuator act;
    PipelineSettings s = BaseSettings();
    s.aim.smoothing.enabled = true;
    s.aim.smoothing.model = "critical";
    s.aim.smoothing.omega = 20.f;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);

    // 帧 1：模型 x=320 → 屏幕 1020 → 几何 delta 60（首帧直通）
    be.dets = { MakeModelDet(320, 300, 40, 80, 0.9f, 0) };
    const auto r1 = p.ProcessFrame(0);
    AIM_EXPECT_TRUE(r1.ok);
    AIM_EXPECT_NEAR(r1.delta_px.x, 60.f, 0.5f);
    AIM_EXPECT_NEAR(act.last.dx_px, 60.f, 0.5f);

    // 帧 2：目标跳到 x=380 → 几何 delta 240；下发值应滞后在 (60, 240) 之间
    be.dets = { MakeModelDet(380, 300, 40, 80, 0.9f, 0) };
    AIM_EXPECT_TRUE(p.ProcessFrame(8).ok);
    const float cmd2 = act.last.dx_px;
    AIM_EXPECT_TRUE(cmd2 > 60.f);
    AIM_EXPECT_TRUE(cmd2 < 240.f);

    // 持续 40 帧（约 320ms，≈6 个时间常数）后应收敛到几何值
    float cmd_last = cmd2;
    for (int i = 0; i < 40; ++i) {
        p.ProcessFrame(16 + i * 8);
        cmd_last = act.last.dx_px;
    }
    AIM_EXPECT_TRUE(cmd_last > cmd2);
    AIM_EXPECT_NEAR(cmd_last, 240.f, 6.f);
}

// 分辨率以「采集器上报的帧尺寸」为准（回归：多显示器/DPI 下识别值可能不是采集的那块屏）。
// 设置里写 1920x1080，帧上报 2560x1600：准星中心必须是 1280,800，否则整条瞄准会偏半屏。
AIM_TEST(pipeline_prefers_frame_screen_size) {
    FakeCapture cap;
    cap.w = 2560; cap.h = 1600;
    cap.screen_w = 2560; cap.screen_h = 1600; // 采集器知道真实尺寸
    cap.src_x = 0; cap.src_y = 0; cap.src_w = 0; cap.src_h = 0; // 帧即整屏

    FakeBackend be;
    FakeActuator act;
    // 模型输入 640x640 → ROI 2560x1600：x 缩放 4、y 缩放 2.5（各向异性，与生产一致）
    // 框 (300,310.4,40,80) → 屏幕 (1200,776,160,200)
    // 头部锚点 = (1200+80, 776+200*0.12) = (1280, 800) = 帧中心
    be.dets.push_back(MakeModelDet(300, 310.4f, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings(); // screen_w/h = 1920x1080（识别值，故意与帧不同）
    s.imgsz = 640;
    s.scope.mode = ScopeMode::Rect;
    s.scope.half_w = 1280.f; s.scope.half_h = 800.f;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);
    const auto r = p.ProcessFrame();

    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_TRUE(r.has_target);
    // 若误用设置里的 1920x1080，中心是 (960,540)，delta 会是 (320,260)
    AIM_EXPECT_NEAR(r.delta_px.x, 0.f, 0.5f);
    AIM_EXPECT_NEAR(r.delta_px.y, 0.f, 0.5f);
}

// 帧尺寸与识别值不一致时按真实宽高比重算 FOV（16:10 的 HFOV 是 97°，不是 16:9 的 103°）
AIM_TEST(pipeline_reestimates_fov_on_size_mismatch) {
    FakeCapture cap;
    cap.w = 2560; cap.h = 1600;
    cap.screen_w = 2560; cap.screen_h = 1600;

    FakeBackend be;
    FakeActuator act;
    // 框 (400,300,40,80) → 屏幕 (1600,1200,160,320)，中心 x = 1680，dx = 400
    be.dets.push_back(MakeModelDet(400, 300, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings();
    s.imgsz = 640;
    s.scope.mode = ScopeMode::Rect;
    s.scope.half_w = 1280.f; s.scope.half_h = 800.f;
    s.fov = Fov{103.f, 70.5f};      // 识别阶段按 16:9 算的值（错的）
    s.fov_cfg = FovConfig{};        // 自动
    s.fov_reestimatable = true;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);
    const auto r = p.ProcessFrame();
    AIM_EXPECT_TRUE(r.ok);

    // f = (2560/2)/tan(97/2) = 1142.6 → angle = atan(400/1142.6) = 19.29°
    const float f = 1280.f / std::tan(Deg2Rad(97.f) * 0.5f);
    const float expect = Rad2Deg(std::atan(400.f / f));
    AIM_EXPECT_NEAR(r.delta_angle_deg.x, expect, 0.05f);
    // 若仍用 103° 会得到 21.45°，必须能区分开
    AIM_EXPECT_TRUE(r.delta_angle_deg.x < 20.5f);
}

// fov_reestimatable=false（手工构造设置/旧调用方）时行为完全不变
AIM_TEST(pipeline_keeps_fov_when_not_reestimatable) {
    FakeCapture cap;
    cap.w = 2560; cap.h = 1600;
    cap.screen_w = 2560; cap.screen_h = 1600;

    FakeBackend be;
    FakeActuator act;
    be.dets.push_back(MakeModelDet(400, 300, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings();
    s.imgsz = 640;
    s.scope.mode = ScopeMode::Rect;
    s.scope.half_w = 1280.f; s.scope.half_h = 800.f;
    s.fov = Fov{103.f, 70.5f};
    // fov_reestimatable 保持默认 false

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);
    const auto r = p.ProcessFrame();
    AIM_EXPECT_TRUE(r.ok);

    const float f = 1280.f / std::tan(Deg2Rad(103.f) * 0.5f);
    const float expect = Rad2Deg(std::atan(400.f / f));
    AIM_EXPECT_NEAR(r.delta_angle_deg.x, expect, 0.05f);
}

// ApplySettings 后必须重算 FOV：否则用户改 manual_override/ads 后，缓存里还是旧值，
// 只有帧尺寸变化才会刷新 → 一直用旧 FOV（本 bug 由修复 B 引入，此处钉死）
AIM_TEST(pipeline_apply_settings_invalidates_fov_cache) {
    FakeCapture cap;
    cap.w = 2560; cap.h = 1600;
    cap.screen_w = 2560; cap.screen_h = 1600;

    FakeBackend be;
    FakeActuator act;
    be.dets.push_back(MakeModelDet(400, 300, 40, 80, 0.9f, 0));

    PipelineSettings s = BaseSettings();
    s.imgsz = 640;
    s.scope.mode = ScopeMode::Rect;
    s.scope.half_w = 1280.f; s.scope.half_h = 800.f;
    s.fov = Fov{103.f, 70.5f};
    s.fov_cfg = FovConfig{};
    s.fov_reestimatable = true;

    Pipeline p;
    p.Configure(PipelineDeps{&cap, &be, &act}, s);
    const auto r1 = p.ProcessFrame(0);
    AIM_EXPECT_TRUE(r1.ok);
    const float f97 = 1280.f / std::tan(Deg2Rad(97.f) * 0.5f);
    AIM_EXPECT_NEAR(r1.delta_angle_deg.x, Rad2Deg(std::atan(400.f / f97)), 0.05f);

    // 改成手动覆盖 80°：ApplySettings 之后必须用 80°
    PipelineSettings s2 = s;
    s2.fov_cfg.manual_override = true;
    s2.fov_cfg.hfov_deg = 80.f;
    s2.fov_cfg.vfov_deg = 50.f;
    p.ApplySettings(s2);
    const auto r2 = p.ProcessFrame(16);
    AIM_EXPECT_TRUE(r2.ok);
    const float f80 = 1280.f / std::tan(Deg2Rad(80.f) * 0.5f);
    AIM_EXPECT_NEAR(r2.delta_angle_deg.x, Rad2Deg(std::atan(400.f / f80)), 0.05f);
}
