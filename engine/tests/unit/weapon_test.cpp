#include "aim_test.h"
#include "weapon/pattern_store.h"
#include "weapon/recoil_engine.h"
#include "weapon/trigger_engine.h"
#include <cmath>

using namespace aim;

AIM_TEST(pattern_store_load_and_sample) {
    const std::string js = R"({
      "patterns":[{"id":"m4a1s","samples":[
        {"t":0,"dx":0.0,"dy":1.0},
        {"t":100,"dx":0.5,"dy":2.0}
      ]}]
    })";
    PatternStore store;
    std::string err;
    AIM_EXPECT_TRUE(store.LoadFromJson(js, &err));
    AIM_EXPECT_EQ(store.Count(), 1u);
    AIM_EXPECT_TRUE(store.Has("m4a1s"));

    Vec2F mid = store.Sample("m4a1s", 50.f);
    AIM_EXPECT_NEAR(mid.x, 0.25f, 1e-4f);
    AIM_EXPECT_NEAR(mid.y, 1.5f, 1e-4f);

    // 端点钳制
    Vec2F before = store.Sample("m4a1s", -10.f);
    AIM_EXPECT_NEAR(before.y, 1.0f, 1e-4f);
    Vec2F after = store.Sample("m4a1s", 500.f);
    AIM_EXPECT_NEAR(after.y, 2.0f, 1e-4f);

    // 缺失 id
    Vec2F miss = store.Sample("nope", 50.f);
    AIM_EXPECT_NEAR(miss.x, 0.f, 1e-6f);
    AIM_EXPECT_NEAR(miss.y, 0.f, 1e-6f);
}

AIM_TEST(pattern_store_roundtrip) {
    PatternStore a;
    RecoilPattern p;
    p.id = "ak47";
    p.samples.push_back(PatternSample{0.f, 0.f, 1.5f});
    p.samples.push_back(PatternSample{80.f, -0.3f, 3.0f});
    a.Add(p);

    std::string js = a.ToJson();
    PatternStore b;
    AIM_EXPECT_TRUE(b.LoadFromJson(js));
    AIM_EXPECT_EQ(b.Count(), 1u);
    Vec2F s = b.Sample("ak47", 40.f);
    AIM_EXPECT_NEAR(s.y, 2.25f, 1e-4f);
    AIM_EXPECT_NEAR(s.x, -0.15f, 1e-4f);
}

AIM_TEST(recoil_engine_math) {
    RecoilParams rp; rp.strength = 5; rp.rpm = 600; rp.k = 0.2f;
    rp.base_px = 300.f; rp.horizontal_gain = 0.f;
    RecoilEngine eng;
    eng.Configure(rp, nullptr, "");
    AIM_EXPECT_NEAR(eng.FireIntervalMs(), 100.f, 1e-4f);
    // 峰值补偿 = base_px × strength/15
    AIM_EXPECT_NEAR(eng.BaseCompensation(), 300.f * 5.f / 15.f, 1e-4f);

    // 无弹道库：按发数线性下压，仅垂直
    Vec2F c = eng.CompensationAt(50.f);   // 半发
    AIM_EXPECT_NEAR(c.x, 0.f, 1e-6f);
    AIM_EXPECT_NEAR(c.y, 0.5f, 1e-5f);    // 0.5 发 × (5×0.2)

    // 强度钳制 [0,30]：99 → 30，峰值 = base_px × 30/15 = 2 倍
    RecoilParams hi = rp; hi.strength = 99.f;
    RecoilEngine eng2; eng2.Configure(hi, nullptr, "");
    AIM_EXPECT_NEAR(eng2.BaseCompensation(), 600.f, 1e-4f);

    // 强度 0：完全不补偿
    RecoilParams zero = rp; zero.strength = 0.f;
    RecoilEngine eng3; eng3.Configure(zero, nullptr, "");
    AIM_EXPECT_NEAR(eng3.BaseCompensation(), 0.f, 1e-6f);
    AIM_EXPECT_NEAR(eng3.CompensationAt(1000.f).y, 0.f, 1e-6f);
}

AIM_TEST(recoil_engine_with_pattern) {
    PatternStore store;
    RecoilPattern p;
    p.id = "p";
    p.samples.push_back(PatternSample{0.f, 0.f, 1.0f});
    p.samples.push_back(PatternSample{100.f, 0.f, 2.0f});
    store.Add(p);

    RecoilParams rp; rp.strength = 15; rp.rpm = 600; rp.k = 0.2f;
    rp.base_px = 100.f; rp.manual_horizontal = false;
    RecoilEngine eng;
    eng.Configure(rp, &store, "p");

    Vec2F c = eng.CompensationAt(50.f);
    AIM_EXPECT_NEAR(c.y, 100.f * 1.5f, 1e-4f); // 峰值(100) × shape(1.5)
}

AIM_TEST(trigger_crosshair_hits) {
    Detection d;
    d.box = RectF{100, 100, 100, 200}; // 中心 (150,200)
    d.score = 0.9f;

    AIM_EXPECT_TRUE(CrosshairHits(d, 150.f, 200.f, 0, 0.35f));
    AIM_EXPECT_TRUE(CrosshairHits(d, 150.f, 220.f, 0, 0.35f));  // 边界（band=20）
    AIM_EXPECT_FALSE(CrosshairHits(d, 150.f, 221.f, 0, 0.35f));
    AIM_EXPECT_FALSE(CrosshairHits(d, 99.f, 200.f, 0, 0.35f));  // x 出框
    AIM_EXPECT_FALSE(CrosshairHits(d, 150.f, 200.f, 0, 0.95f)); // score 不足

    // aim_offset 上移判定线 20px
    AIM_EXPECT_TRUE(CrosshairHits(d, 150.f, 180.f, -20, 0.35f));
}

AIM_TEST(trigger_disabled_or_no_key) {
    TriggerParams p; p.enabled = false; p.key = "MOUSE5";
    TriggerEngine e; e.Configure(p);
    TriggerInput in; in.key_down = true; in.crosshair_hit = true; in.target_score = 0.9f;
    auto o = e.Update(in, 100.f);
    AIM_EXPECT_FALSE(o.fire_down);

    TriggerParams p2; p2.enabled = true; p2.key = ""; // 未设键 → 不生效
    TriggerEngine e2; e2.Configure(p2);
    auto o2 = e2.Update(in, 100.f);
    AIM_EXPECT_FALSE(o2.fire_down);
}

AIM_TEST(trigger_rifle_delay_and_release) {
    TriggerParams p; p.enabled = true; p.key = "MOUSE5";
    p.type = TriggerType::Rifle; p.rifle_delay_ms = 70;
    TriggerEngine e; e.Configure(p);

    TriggerInput in; in.key_down = true; in.crosshair_hit = true; in.target_score = 0.9f;

    AIM_EXPECT_FALSE(e.Update(in, 30.f).fire_down); // 30
    AIM_EXPECT_FALSE(e.Update(in, 30.f).fire_down); // 60
    AIM_EXPECT_TRUE(e.Update(in, 30.f).fire_down);  // 90 ≥ 70

    TriggerInput lost = in; lost.crosshair_hit = false;
    auto up = e.Update(lost, 10.f);
    AIM_EXPECT_TRUE(up.fire_up);
    AIM_EXPECT_FALSE(e.Firing());
}

AIM_TEST(trigger_sniper_single_shot) {
    TriggerParams p; p.enabled = true; p.key = "MOUSE5";
    p.type = TriggerType::Sniper; p.sniper_delay_ms = 0;
    TriggerEngine e; e.Configure(p);

    TriggerInput in; in.key_down = true; in.crosshair_hit = true; in.target_score = 0.9f;
    auto o1 = e.Update(in, 10.f);
    AIM_EXPECT_TRUE(o1.fire_down);

    auto o2 = e.Update(in, 40.f); // 超过脉冲宽度 → 抬起
    AIM_EXPECT_TRUE(o2.fire_up);
    AIM_EXPECT_FALSE(e.Firing());
}

int main() {
    return aim::test::run_all();
}
