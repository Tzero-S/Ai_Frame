#include "aim_test.h"
#include "aim/scope.h"
#include "aim/target_point.h"
#include "aim/target_select.h"
#include "aim/aim_calc.h"
#include "aim/smoothing.h"
#include "aim/recoil_micro.h"
#include <cmath>
#include <vector>

using namespace aim;

static Detection MakeDet(float x, float y, float w, float h, float score, int cls = 0) {
    Detection d;
    d.box = RectF{x, y, w, h};
    d.score = score;
    d.class_id = cls;
    return d;
}

AIM_TEST(scope_rect_inside_outside) {
    Fov fov{};
    ScopeSpec s; s.mode = ScopeMode::Rect; s.half_w = 320.f; s.half_h = 240.f;
    Detection center = MakeDet(900, 500, 120, 200, 0.9f); // aim_point=(960,600)=屏幕中心
    AIM_EXPECT_TRUE(InScope(center, s, 1920, 1080, fov));

    Detection far = MakeDet(1300, 500, 120, 200, 0.9f);   // dx=400>320
    AIM_EXPECT_FALSE(InScope(far, s, 1920, 1080, fov));

    Detection edge = MakeDet(1220, 500, 120, 200, 0.9f);  // dx=320 == half_w
    AIM_EXPECT_TRUE(InScope(edge, s, 1920, 1080, fov));
}

AIM_TEST(scope_circle) {
    Fov fov{};
    ScopeSpec s; s.mode = ScopeMode::Circle; s.radius_px = 100.f;
    AIM_EXPECT_TRUE(InScope(MakeDet(940, 500, 120, 200, 0.9f), s, 1920, 1080, fov));  // dx=80
    AIM_EXPECT_FALSE(InScope(MakeDet(1000, 500, 120, 200, 0.9f), s, 1920, 1080, fov)); // dx=120
}

AIM_TEST(scope_fov_angle_mode) {
    Fov fov{}; // 103 / 59
    const float fx = fov.FocalX(1920);
    const float fy = fov.FocalY(1080);
    ScopeSpec s; s.mode = ScopeMode::Fov; s.hfov_half_deg = 10.f; s.vfov_half_deg = 10.f;

    // 5° 处（框中心相对屏幕中心偏移 fx*tan(5°)）
    float dx5 = fx * std::tan(Deg2Rad(5.f));
    Detection inside = MakeDet(960.f + dx5 - 60.f, 500, 120, 200, 0.9f);
    AIM_EXPECT_TRUE(InScope(inside, s, 1920, 1080, fov));

    // 15° 处
    float dx15 = fx * std::tan(Deg2Rad(15.f));
    Detection outside = MakeDet(960.f + dx15 - 60.f, 500, 120, 200, 0.9f);
    AIM_EXPECT_FALSE(InScope(outside, s, 1920, 1080, fov));

    (void)fy;
}

AIM_TEST(target_point_presets_and_micro) {
    Detection d = MakeDet(900, 500, 120, 200, 0.9f); // cx=960, y1=500, h=200
    AimConfig::MicroAdjust none{};

    PointF head = ComputeTargetPoint(d, TargetPreset::Head, 0.12f, none);
    AIM_EXPECT_NEAR(head.x, 960.f, 1e-3f);
    AIM_EXPECT_NEAR(head.y, 500.f + 0.12f * 200.f, 1e-3f); // 524

    PointF chest = ComputeTargetPoint(d, TargetPreset::Chest, 0.12f, none);
    AIM_EXPECT_NEAR(chest.y, 500.f + 0.35f * 200.f, 1e-3f); // 570

    PointF custom = ComputeTargetPoint(d, TargetPreset::Custom, 0.60f, none);
    AIM_EXPECT_NEAR(custom.y, 500.f + 0.60f * 200.f, 1e-3f); // 620

    AimConfig::MicroAdjust ma{}; ma.up = 3; ma.left = 4;
    PointF adj = ComputeTargetPoint(d, TargetPreset::Head, 0.12f, ma);
    AIM_EXPECT_NEAR(adj.x, 960.f - 4.f, 1e-3f);
    AIM_EXPECT_NEAR(adj.y, 524.f - 3.f, 1e-3f);
}

AIM_TEST(target_select_strategies) {
    Fov fov{};
    ScopeSpec s; s.mode = ScopeMode::Rect; s.half_w = 320.f; s.half_h = 240.f;
    std::vector<Detection> dets;
    dets.push_back(MakeDet(900, 500, 120, 200, 0.50f));  // A 中心
    dets.push_back(MakeDet(1000, 500, 120, 200, 0.90f)); // B 偏右100
    dets.push_back(MakeDet(1900, 500, 120, 200, 0.99f)); // C 出范围

    const Detection* near = SelectTarget(dets, TargetSelection::NearestCrosshair, s, 1920, 1080, fov);
    AIM_EXPECT_TRUE(near == &dets[0]);

    const Detection* best = SelectTarget(dets, TargetSelection::HighestScore, s, 1920, 1080, fov);
    AIM_EXPECT_TRUE(best == &dets[1]); // C 不在范围内

    const Detection* sticky = SelectTarget(dets, TargetSelection::Sticky, s, 1920, 1080, fov, &dets[1]);
    AIM_EXPECT_TRUE(sticky == &dets[1]);

    // 锁定目标已出范围 → 退回最近
    const Detection* fallback = SelectTarget(dets, TargetSelection::Sticky, s, 1920, 1080, fov, &dets[2]);
    AIM_EXPECT_TRUE(fallback == &dets[0]);
}

AIM_TEST(aim_calc_pixel_and_angle) {
    Fov fov{}; // 103/59
    AimConfig aim{}; // head 0.12, 无微调
    Detection d = MakeDet(900, 500, 120, 200, 0.9f);

    AimResult r = ComputeAim(d, aim, 1920, 1080, fov);
    AIM_EXPECT_NEAR(r.delta_px.x, 0.f, 1e-3f);
    AIM_EXPECT_NEAR(r.delta_px.y, 524.f - 540.f, 1e-3f); // -16

    // 角度：atan(-16 / fy)，fy=(1080/2)/tan(59/2)
    const float fy = fov.FocalY(1080);
    const float expect_ay = Rad2Deg(std::atan(-16.f / fy));
    AIM_EXPECT_NEAR(r.delta_angle_deg.y, expect_ay, 1e-3f);
    AIM_EXPECT_NEAR(r.delta_angle_deg.x, 0.f, 1e-4f);
}

AIM_TEST(aim_calc_recoil_injection) {
    Fov fov{};
    AimConfig aim{};
    Detection d = MakeDet(900, 500, 120, 200, 0.9f);
    AimResult base = ComputeAim(d, aim, 1920, 1080, fov);
    AimResult comp = ComputeAim(d, aim, 1920, 1080, fov, Vec2F{0.f, 5.f}); // 向下补偿5px
    AIM_EXPECT_NEAR(comp.delta_px.y, base.delta_px.y + 5.f, 1e-3f);
}

AIM_TEST(smoothing_ema) {
    SmoothingFilter sf;
    sf.model = SmoothingModel::Ema; // 本用例专测 EMA 分支
    sf.alpha = 0.5f;
    Vec2F a = sf.Apply(Vec2F{10.f, 20.f});
    AIM_EXPECT_NEAR(a.x, 10.f, 1e-4f);
    Vec2F b = sf.Apply(Vec2F{20.f, 40.f});
    AIM_EXPECT_NEAR(b.x, 15.f, 1e-4f); // 10 + 0.5*(20-10)
    AIM_EXPECT_NEAR(b.y, 30.f, 1e-4f);
    sf.Reset();
    Vec2F c = sf.Apply(Vec2F{100.f, 0.f});
    AIM_EXPECT_NEAR(c.x, 100.f, 1e-4f);
}

AIM_TEST(recoil_micro_compensation) {
    AimConfig::RecoilMicro rm;
    rm.enabled = true; rm.vertical = 5.f; rm.horizontal = -3.f;
    Vec2F c = RecoilMicroCompensation(rm);
    AIM_EXPECT_NEAR(c.x, -3.f, 1e-4f);
    AIM_EXPECT_NEAR(c.y, 5.f, 1e-4f);

    rm.enabled = false;
    Vec2F z = RecoilMicroCompensation(rm);
    AIM_EXPECT_NEAR(z.x, 0.f, 1e-4f);
    AIM_EXPECT_NEAR(z.y, 0.f, 1e-4f);
}

int main() {
    return aim::test::run_all();
}
