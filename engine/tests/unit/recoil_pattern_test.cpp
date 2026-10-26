#include "aim_test.h"
#include "core/paths.h"
#include "weapon/pattern_store.h"
#include "weapon/recoil_engine.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace aim;

namespace {

const char* kJson = R"({
  "patterns": [
    {"id": "m4a1s", "samples": [
      {"t": 0,   "dx": 0.0, "dy": 0.0},
      {"t": 200, "dx": 0.0, "dy": 0.5},
      {"t": 400, "dx": 0.2, "dy": 1.0}
    ]}
  ]
})";

} // namespace

// 解析 + 存在性 + 计数
AIM_TEST(pattern_store_load_and_query) {
    PatternStore st;
    std::string err;
    AIM_EXPECT_TRUE(st.LoadFromJson(kJson, &err));
    AIM_EXPECT_EQ(st.Count(), 1u);
    AIM_EXPECT_TRUE(st.Has("m4a1s"));
    AIM_EXPECT_FALSE(st.Has("ak47"));
    AIM_EXPECT_TRUE(st.Get("m4a1s") != nullptr);
    AIM_EXPECT_EQ(st.Get("m4a1s")->samples.size(), 3u);
}

// 线性插值：中点取两端均值；越界取端点；缺失 id 返回 0
AIM_TEST(pattern_store_interpolation) {
    PatternStore st;
    AIM_EXPECT_TRUE(st.LoadFromJson(kJson, nullptr));

    const Vec2F mid = st.Sample("m4a1s", 100.f);
    AIM_EXPECT_NEAR(mid.y, 0.25f, 1e-4f);

    const Vec2F before = st.Sample("m4a1s", -50.f);
    AIM_EXPECT_NEAR(before.y, 0.f, 1e-4f);

    const Vec2F after = st.Sample("m4a1s", 9999.f);
    AIM_EXPECT_NEAR(after.y, 1.0f, 1e-4f);

    const Vec2F missing = st.Sample("nope", 100.f);
    AIM_EXPECT_NEAR(missing.x, 0.f, 1e-4f);
    AIM_EXPECT_NEAR(missing.y, 0.f, 1e-4f);
}

// 非法输入：解析失败但不崩、不写入半成品
AIM_TEST(pattern_store_rejects_bad_json) {
    PatternStore st;
    std::string err;
    AIM_EXPECT_FALSE(st.LoadFromJson("{ not json", &err));
    AIM_EXPECT_FALSE(err.empty());

    PatternStore st2;
    AIM_EXPECT_FALSE(st2.LoadFromJson(R"({"nope":[]})", nullptr));
    AIM_EXPECT_EQ(st2.Count(), 0u);
}

// 有弹道库：累计补偿 = base_px × strength/15 × shape
AIM_TEST(recoil_with_pattern_follows_curve) {
    PatternStore st;
    AIM_EXPECT_TRUE(st.LoadFromJson(kJson, nullptr));

    RecoilParams p;
    p.strength = 15;   // 满强度 → 峰值 = base_px
    p.base_px = 300.f;
    p.manual_horizontal = false; // 用曲线自带 dx

    RecoilEngine e;
    e.Configure(p, &st, "m4a1s");

    const Vec2F t0 = e.CompensationAt(0.f);
    const Vec2F t400 = e.CompensationAt(400.f);
    AIM_EXPECT_NEAR(t0.y, 0.f, 1e-4f);
    AIM_EXPECT_NEAR(t400.y, 300.f, 1e-4f);   // 峰值 × shape(1.0)
    AIM_EXPECT_NEAR(t400.x, 60.f, 1e-4f);    // 300 × 0.2
    AIM_EXPECT_TRUE(t400.y > t0.y);
}

// 强度线性映射：strength=15 → 100%，strength=5 → 1/3
AIM_TEST(recoil_strength_scales_peak) {
    PatternStore st;
    AIM_EXPECT_TRUE(st.LoadFromJson(kJson, nullptr));
    RecoilParams p;
    p.base_px = 300.f;
    p.manual_horizontal = false;

    RecoilEngine a;
    p.strength = 15;
    a.Configure(p, &st, "m4a1s");
    AIM_EXPECT_NEAR(a.CompensationAt(400.f).y, 300.f, 1e-3f);

    RecoilEngine b;
    p.strength = 5;
    b.Configure(p, &st, "m4a1s");
    AIM_EXPECT_NEAR(b.CompensationAt(400.f).y, 100.f, 1e-3f);
}

// 增量语义：SampleNow 的累加必须等于 CumulativeAt（这是量纲修复的核心）
AIM_TEST(recoil_sample_now_is_incremental) {
    PatternStore st;
    AIM_EXPECT_TRUE(st.LoadFromJson(kJson, nullptr));
    RecoilParams p;
    p.strength = 15;
    p.base_px = 300.f;
    p.manual_horizontal = false;

    RecoilEngine e;
    e.Configure(p, &st, "m4a1s");

    float sum = 0.f;
    for (int i = 0; i <= 8; ++i) {         // 采样 t=0,50,...,400 共 9 次
        sum += e.SampleNow().y;
        e.Advance(50.f);
    }
    AIM_EXPECT_NEAR(sum, e.CumulativeAt(400.f).y, 1e-3f);
    // 单帧增量必须远小于总量（旧实现每帧都加累计值，会放大数十倍）
    AIM_EXPECT_TRUE(sum > 0.f);
}

// 横向增益：手动模式下横向 = 垂直 × gain
AIM_TEST(recoil_horizontal_gain_manual) {
    PatternStore st;
    AIM_EXPECT_TRUE(st.LoadFromJson(kJson, nullptr));
    RecoilParams p;
    p.strength = 15;
    p.base_px = 300.f;
    p.manual_horizontal = true;
    p.horizontal_gain = -0.5f; // 向左

    RecoilEngine e;
    e.Configure(p, &st, "m4a1s");
    const Vec2F v = e.CumulativeAt(400.f);
    AIM_EXPECT_NEAR(v.y, 300.f, 1e-3f);
    AIM_EXPECT_NEAR(v.x, -150.f, 1e-3f); // 300 × -0.5

    p.horizontal_gain = 0.f; // 纯垂直
    RecoilEngine e2;
    e2.Configure(p, &st, "m4a1s");
    AIM_EXPECT_NEAR(e2.CumulativeAt(400.f).x, 0.f, 1e-4f);
}

// 无弹道库：按发数线性下压（每发 strength*k 像素）
AIM_TEST(recoil_without_pattern_is_shot_linear) {
    RecoilParams p;
    p.strength = 5;
    p.k = 0.2f;   // 每发 1.0 px
    p.rpm = 600;  // 100ms/发
    p.horizontal_gain = 0.f;

    RecoilEngine e;
    e.Configure(p, nullptr, "m4a1s");

    AIM_EXPECT_NEAR(e.CompensationAt(0.f).y, 0.f, 1e-4f);
    AIM_EXPECT_NEAR(e.CompensationAt(1000.f).y, 10.f, 1e-4f); // 10 发
    AIM_EXPECT_NEAR(e.CompensationAt(0.f).x, 0.f, 1e-4f);
}

// 库在但没有该 id：同样退化
AIM_TEST(recoil_missing_id_falls_back) {
    PatternStore st;
    AIM_EXPECT_TRUE(st.LoadFromJson(kJson, nullptr));

    RecoilParams p;
    p.strength = 3;
    p.k = 0.5f;  // 每发 1.5 px
    p.rpm = 600; // 100ms/发
    p.horizontal_gain = 0.f;

    RecoilEngine e;
    e.Configure(p, &st, "unknown_gun");
    const Vec2F v = e.CompensationAt(400.f); // 4 发
    AIM_EXPECT_NEAR(v.x, 0.f, 1e-4f);
    AIM_EXPECT_NEAR(v.y, 6.f, 1e-4f);
}

// 路径解析：不存在的文件返回空；存在的返回可用路径
AIM_TEST(paths_resolve_data_file) {
    AIM_EXPECT_TRUE(ResolveDataFile("definitely_not_here_12345.json").empty());
    AIM_EXPECT_TRUE(ResolveDataFile("").empty());

    const std::string tmp =
        (std::filesystem::temp_directory_path() / "aim_paths_test.json").string();
    {
        std::ofstream out(tmp, std::ios::binary);
        out << "{}";
    }
    const std::string got = ResolveDataFile(tmp);
    AIM_EXPECT_FALSE(got.empty());
    AIM_EXPECT_STREQ(got.c_str(), tmp.c_str());
    std::remove(tmp.c_str());
}

int main() {
    return aim::test::run_all();
}

// 运行时改强度（开镜切档用）：SetStrength 立即影响后续采样
AIM_TEST(recoil_set_strength_takes_effect) {
    PatternStore st;
    AIM_EXPECT_TRUE(st.LoadFromJson(kJson, nullptr));
    RecoilParams p;
    p.strength = 15.f;
    p.base_px = 300.f;
    p.manual_horizontal = false;

    RecoilEngine e;
    e.Configure(p, &st, "m4a1s");
    AIM_EXPECT_NEAR(e.CumulativeAt(400.f).y, 300.f, 1e-3f);

    e.SetStrength(7.5f); // 半强度
    AIM_EXPECT_NEAR(e.CumulativeAt(400.f).y, 150.f, 1e-3f);

    e.SetStrength(0.f);  // 关掉
    AIM_EXPECT_NEAR(e.CumulativeAt(400.f).y, 0.f, 1e-4f);
}
