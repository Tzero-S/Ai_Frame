#include "aim_test.h"
#include "aim/aim_calc.h"
#include "aim/scope.h"
#include "aim/target_select.h"
#include "fov/fov_estimator.h"
#include "weapon/trigger_engine.h"

#include <cmath>

using namespace aim;

namespace {

Detection MakeDet(float x, float y, float w, float h, float score = 0.9f, int cls = 0) {
    Detection d;
    d.box = RectF{ x, y, w, h };
    d.score = score;
    d.class_id = cls;
    return d;
}

} // namespace

// 准星偏移：基准点右移 50px 后，同一目标的水平位移相应左移 50px
AIM_TEST(crosshair_offset_shifts_aim_delta) {
    const Detection d = MakeDet(860.f, 440.f, 200.f, 200.f);

    AimConfig base;
    base.target_point.preset = TargetPreset::Chest; // 用胸部预设，锚点 x 即框中心
    const AimResult r0 = ComputeAim(d, base, 1920.f, 1080.f, Fov{});

    AimConfig shifted = base;
    shifted.crosshair_offset_x = 50.f;
    const AimResult r1 = ComputeAim(d, shifted, 1920.f, 1080.f, Fov{});

    AIM_EXPECT_NEAR(r1.delta_px.x - r0.delta_px.x, -50.f, 1e-3f);
    // 纵向不受影响
    AIM_EXPECT_NEAR(r1.delta_px.y, r0.delta_px.y, 1e-3f);
}

// 范围过滤：以准星实际位置为中心时，边缘目标才进范围
AIM_TEST(scope_uses_crosshair_center) {
    const Detection d = MakeDet(1200.f, 440.f, 200.f, 200.f); // 中心 x=1300
    ScopeSpec s;
    s.mode = ScopeMode::Rect;
    s.half_w = 320.f;
    s.half_h = 240.f;

    const Vec2F shifted{ 1010.f, 540.f };
    AIM_EXPECT_TRUE(InScope(d, s, 1920.f, 1080.f, Fov{}, &shifted));   // dx=290
    AIM_EXPECT_FALSE(InScope(d, s, 1920.f, 1080.f, Fov{}));            // dx=340（屏幕中心）
}

// 目标选择：最近准星策略在偏移中心下选中不同的目标
AIM_TEST(target_select_uses_crosshair_center) {
    std::vector<Detection> dets;
    dets.push_back(MakeDet(660.f, 440.f, 200.f, 200.f, 0.9f, 0));  // 中心 760
    dets.push_back(MakeDet(1060.f, 440.f, 200.f, 200.f, 0.9f, 1)); // 中心 1160

    ScopeSpec s;
    s.mode = ScopeMode::Rect;
    s.half_w = 600.f;
    s.half_h = 400.f;

    // 屏幕中心 960：|760-960|=200 vs |1160-960|=200 → 距离相同，取先出现者
    const Detection* a = SelectTarget(dets, TargetSelection::NearestCrosshair, s, 1920.f, 1080.f,
                                      Fov{}, nullptr);
    AIM_EXPECT_TRUE(a != nullptr);
    if (a) AIM_EXPECT_EQ(a->class_id, 0);

    // 准星右移到 1100：目标 1 更近
    const Vec2F center{ 1100.f, 540.f };
    const Detection* b = SelectTarget(dets, TargetSelection::NearestCrosshair, s, 1920.f, 1080.f,
                                      Fov{}, nullptr, &center);
    AIM_EXPECT_TRUE(b != nullptr);
    if (b) AIM_EXPECT_EQ(b->class_id, 1);
}

// 开镜倍率表：命中表则覆盖 FOV；腰射/空表保持原行为
AIM_TEST(fov_ads_table_interpolates) {
    FovConfig cfg;
    cfg.ads_levels = { { 1.f, 103.f }, { 2.f, 51.f }, { 4.f, 26.f } };

    cfg.ads_zoom = 1.f;
    const FovEstimate hip = EstimateFov(1920, 1080, cfg);
    AIM_EXPECT_FALSE(hip.ads_applied);
    AIM_EXPECT_NEAR(hip.fov.hfov_deg, 103.f, 0.5f);

    cfg.ads_zoom = 2.f;
    const FovEstimate x2 = EstimateFov(1920, 1080, cfg);
    AIM_EXPECT_TRUE(x2.ads_applied);
    AIM_EXPECT_NEAR(x2.fov.hfov_deg, 51.f, 0.5f);
    AIM_EXPECT_TRUE(x2.fov.vfov_deg < hip.fov.vfov_deg);

    cfg.ads_zoom = 3.f; // 2x 与 4x 之间插值
    const FovEstimate x3 = EstimateFov(1920, 1080, cfg);
    AIM_EXPECT_TRUE(x3.ads_applied);
    AIM_EXPECT_NEAR(x3.fov.hfov_deg, 38.5f, 0.5f);

    FovConfig empty;
    empty.ads_zoom = 2.f; // 表为空 → 不生效
    const FovEstimate none = EstimateFov(1920, 1080, empty);
    AIM_EXPECT_FALSE(none.ads_applied);
}

// 扳机命中带：比例可配，放大后原本差一点的准星也算命中
AIM_TEST(trigger_hit_band_ratio_configurable) {
    const Detection d = MakeDet(860.f, 440.f, 200.f, 200.f); // 框高 200，中心线 540
    const float cx = 960.f;
    const float cy = 555.f; // 偏离判定线 15px

    AIM_EXPECT_TRUE(CrosshairHits(d, cx, cy, 0, 0.35f, 0.10f));  // band=20 → 命中
    AIM_EXPECT_FALSE(CrosshairHits(d, cx, cy, 0, 0.35f, 0.05f)); // band=10 → 不命中
    // 默认参数沿用常量
    AIM_EXPECT_TRUE(CrosshairHits(d, cx, 541.f, 0, 0.35f));
}

int main() {
    return aim::test::run_all();
}
