#include "aim_test.h"
#include "fov/fov_math.h"
#include "fov/fov_estimator.h"
#include "fov/resolution_detect.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace aim;

AIM_TEST(fov_aspect_lookup_table) {
    AIM_EXPECT_NEAR(HfovFromAspect(4.f / 3.f), 90.f, 0.2f);        // 4:3
    // 16:10 锚点：由 16:9 的 103° 反推 2·atan(tan(103°/2)·1.6/1.7778) ≈ 97.0°。
    // 历史 bug：这里曾是 110°，比 16:9 的 103° 还大，导致更窄的屏幕有更宽的水平视场。
    AIM_EXPECT_NEAR(HfovFromAspect(1.6f), 97.f, 0.2f);             // 16:10
    AIM_EXPECT_NEAR(HfovFromAspect(16.f / 9.f), 103.f, 0.2f);      // 16:9
    AIM_EXPECT_NEAR(HfovFromAspect(21.f / 9.f), 118.f, 0.2f);      // 21:9
}

// 回归护栏：HFOV 必须随宽高比单调不减。
// 屏幕越宽，水平视场不可能越窄——之前的 16:10=110° 让 (1.60,1.78) 区间出现 18 个下降点。
AIM_TEST(fov_aspect_monotonic) {
    float prev = -1.f;
    float prev_aspect = 0.f;
    int violations = 0;
    for (int i = 100; i <= 300; ++i) { // aspect 1.00 → 3.00
        const float a = static_cast<float>(i) / 100.f;
        const float h = HfovFromAspect(a);
        if (h < prev - 1e-4f) {
            ++violations;
            if (violations <= 3) {
                std::printf("    aspect %.2f: HFOV %.2f < 上一点 %.2f (aspect %.2f)\n", a, h, prev,
                            prev_aspect);
            }
        }
        prev = h;
        prev_aspect = a;
    }
    AIM_EXPECT_EQ(violations, 0);
}

// 回归护栏：同一款游戏的垂直视场不因屏幕比例而剧变。
// 16:9 的 103° → VFOV 70.5°；若 16:10 算出 83.5°，说明 HFOV 锚点错了。
AIM_TEST(fov_vfov_consistent_across_aspects) {
    struct Case { int w, h; const char* name; };
    const Case cases[] = {
        {1920, 1440, "4:3"},  {1680, 1050, "16:10"}, {1920, 1200, "16:10"},
        {2560, 1600, "16:10"}, {1920, 1080, "16:9"},  {2560, 1440, "16:9"},
        {3440, 1440, "21:9"},
    };
    for (const auto& c : cases) {
        FovConfig cfg;
        const FovEstimate e = EstimateFov(c.w, c.h, cfg);
        // 现实 FPS 的 VFOV 在 60~76 度区间；越界说明查表锚点不自洽
        AIM_EXPECT_TRUE(e.fov.vfov_deg > 60.f && e.fov.vfov_deg < 76.f);
        // HFOV 与 VFOV 必须满足针孔关系（宽高比 = tan(hfov/2)/tan(vfov/2)）
        const float aspect = static_cast<float>(c.w) / static_cast<float>(c.h);
        const float implied = std::tan(Deg2Rad(e.fov.hfov_deg) * 0.5f) /
                              std::tan(Deg2Rad(e.fov.vfov_deg) * 0.5f);
        AIM_EXPECT_NEAR(implied, aspect, 1e-3f);
    }
}

AIM_TEST(fov_vfov_formula) {
    const float aspect = 16.f / 9.f;
    const float hfov = 103.f;
    const float vfov = VfovFromHfov(hfov, aspect);
    // 与公式 vfov = 2·atan(tan(hfov/2)/aspect) 自洽
    const float expect = Rad2Deg(2.f * std::atan(std::tan(Deg2Rad(hfov) * 0.5f) / aspect));
    AIM_EXPECT_NEAR(vfov, expect, 1e-4f);
    // 16:9 + 103° 的公式结果约 70.5°（§13.2 的 59° 实为 ~2.22:1）
    AIM_EXPECT_NEAR(vfov, 70.5f, 0.3f);
}

AIM_TEST(fov_estimate_auto) {
    FovConfig cfg; // auto_fov=true, manual_override=false
    FovEstimate e = EstimateFov(1920, 1080, cfg);
    AIM_EXPECT_FALSE(e.manual);
    AIM_EXPECT_NEAR(e.fov.hfov_deg, 103.f, 0.2f);
    AIM_EXPECT_NEAR(e.fov.vfov_deg, 70.5f, 0.3f);
}

AIM_TEST(fov_estimate_manual_override) {
    FovConfig cfg;
    cfg.manual_override = true;
    cfg.hfov_deg = 90.f;
    cfg.vfov_deg = 59.f;
    FovEstimate e = EstimateFov(1920, 1080, cfg);
    AIM_EXPECT_TRUE(e.manual);
    AIM_EXPECT_NEAR(e.fov.hfov_deg, 90.f, 1e-4f);
    AIM_EXPECT_NEAR(e.fov.vfov_deg, 59.f, 1e-4f);
    AIM_EXPECT_TRUE(e.inconsistent); // |90-103|/103 ≈ 12.6% > 5%

    cfg.hfov_deg = 103.f;
    FovEstimate e2 = EstimateFov(1920, 1080, cfg);
    AIM_EXPECT_FALSE(e2.inconsistent);
}

AIM_TEST(fov_angle_conversion) {
    Fov f{103.f, 70.5f};
    const float fx = f.FocalX(1920);
    const float dx = fx * std::tan(Deg2Rad(5.f));
    AIM_EXPECT_NEAR(Rad2Deg(AngleX(dx, fx)), 5.f, 1e-3f);

    const float fy = f.FocalY(1080);
    const float dy = fy * std::tan(Deg2Rad(-7.f));
    AIM_EXPECT_NEAR(Rad2Deg(AngleY(dy, fy)), -7.f, 1e-3f);
}

// ---- 分辨率识别（§5.8.1）----

// 识别失败必须**不改写**出参：调用方靠这一点判断能不能用识别值。
AIM_TEST(resolution_detect_contract) {
    int w = 1234, h = 5678;
    // 越界序号在 Win32 上必定失败；非 Windows 上所有查询都失败
    const bool ok = GetMonitorResolution(-1, w, h);
    AIM_EXPECT_FALSE(ok);
    AIM_EXPECT_EQ(w, 1234);
    AIM_EXPECT_EQ(h, 5678);

    const bool ok2 = GetMonitorResolution(9999, w, h);
    AIM_EXPECT_FALSE(ok2);
    AIM_EXPECT_EQ(w, 1234);
    AIM_EXPECT_EQ(h, 5678);
}

AIM_TEST(resolution_detect_primary) {
    int w = 0, h = 0;
    const bool ok = GetPrimaryResolution(w, h);
#ifdef _WIN32
    // 桌面会话下必然能取到；值必须是正数
    AIM_EXPECT_TRUE(ok);
    AIM_EXPECT_TRUE(w > 0);
    AIM_EXPECT_TRUE(h > 0);
    // 与本机显示器序号 0 应一致（同一块屏）
    int w0 = 0, h0 = 0;
    if (GetMonitorResolution(0, w0, h0)) {
        AIM_EXPECT_EQ(w, w0);
        AIM_EXPECT_EQ(h, h0);
    }
#else
    // 非 Windows 不再伪造 1920x1080，必须如实失败
    AIM_EXPECT_FALSE(ok);
#endif
}

AIM_TEST(resolution_enumerate_modes) {
    std::vector<DisplayMode> modes(512);
    const int n = EnumerateDisplayModes(modes.data(), static_cast<int>(modes.size()));
#ifdef _WIN32
    AIM_EXPECT_TRUE(n > 0);
    // 去重：同一分辨率只出现一次（旧实现按刷新率/色深重复，几百项）
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            AIM_EXPECT_FALSE(modes[i].w == modes[j].w && modes[i].h == modes[j].h);
        }
    }
    // 排序：面积升序
    for (int i = 1; i < n; ++i) {
        const long long a = static_cast<long long>(modes[i - 1].w) * modes[i - 1].h;
        const long long b = static_cast<long long>(modes[i].w) * modes[i].h;
        AIM_EXPECT_TRUE(a <= b);
    }
    // max_count 生效
    std::vector<DisplayMode> few(3);
    AIM_EXPECT_TRUE(EnumerateDisplayModes(few.data(), 3) <= 3);
#else
    AIM_EXPECT_EQ(n, 0);
#endif
    // 非法参数不崩
    AIM_EXPECT_EQ(EnumerateDisplayModes(nullptr, 10), 0);
    AIM_EXPECT_EQ(EnumerateDisplayModes(modes.data(), 0), 0);
}

int main() {
    return aim::test::run_all();
}
