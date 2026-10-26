#include "aim_test.h"
#include "aim/smoothing.h"

#include <cmath>

using namespace aim;

// 首帧直通：不引入启动延迟（单帧场景/预览行为不变）
AIM_TEST(smoothing_first_call_passthrough) {
    SmoothingFilter f;
    const Vec2F out = f.Apply(Vec2F{42.f, -17.f}, 0.008f);
    AIM_EXPECT_NEAR(out.x, 42.f, 1e-4f);
    AIM_EXPECT_NEAR(out.y, -17.f, 1e-4f);
}

// 临界阻尼：阶跃响应不过冲（任何时刻不超过目标值），且最终收敛
AIM_TEST(smoothing_critical_no_overshoot) {
    SmoothingFilter f;
    f.model = SmoothingModel::Critical;
    f.omega = 20.f;

    f.Apply(Vec2F{0.f, 0.f}, 0.008f); // 首帧在原点
    float max_seen = 0.f;
    for (int i = 0; i < 200; ++i) { // 200 × 8ms = 1.6s
        const Vec2F out = f.Apply(Vec2F{100.f, 0.f}, 0.008f);
        max_seen = std::max(max_seen, out.x);
    }
    AIM_EXPECT_TRUE(max_seen <= 100.5f); // 允许 0.5px 数值余量，不允许明显过冲
    AIM_EXPECT_NEAR(f.state.x, 100.f, 0.5f);
}

// 临界阻尼：大 dt（掉帧）不发散
AIM_TEST(smoothing_critical_large_dt_stable) {
    SmoothingFilter f;
    f.model = SmoothingModel::Critical;
    f.omega = 60.f; // 高频，对步长最敏感
    f.Apply(Vec2F{0.f, 0.f}, 0.008f);

    for (int i = 0; i < 50; ++i) {
        const Vec2F out = f.Apply(Vec2F{100.f, 100.f}, 0.1f); // 100ms 一步
        AIM_EXPECT_TRUE(std::isfinite(out.x));
        AIM_EXPECT_TRUE(out.x > -1.f && out.x < 200.f);
    }
    AIM_EXPECT_NEAR(f.state.x, 100.f, 2.f);
}

// 非法 dt：按默认步长处理，不产生 NaN
AIM_TEST(smoothing_bad_dt_guarded) {
    SmoothingFilter f;
    f.model = SmoothingModel::Critical;
    f.Apply(Vec2F{0.f, 0.f}, 0.008f);
    const Vec2F a = f.Apply(Vec2F{50.f, 50.f}, 0.f);
    const Vec2F b = f.Apply(Vec2F{50.f, 50.f}, -1.f);
    AIM_EXPECT_TRUE(std::isfinite(a.x));
    AIM_EXPECT_TRUE(std::isfinite(b.x));
}

// jerk 限幅：首帧加速度被压住，收敛更慢但单调
AIM_TEST(smoothing_jerk_limited) {
    SmoothingFilter free_f;
    free_f.model = SmoothingModel::Critical;
    free_f.omega = 20.f;
    free_f.Apply(Vec2F{0.f, 0.f}, 0.008f);
    const Vec2F fast = free_f.Apply(Vec2F{100.f, 0.f}, 0.008f);

    SmoothingFilter limited;
    limited.model = SmoothingModel::Critical;
    limited.omega = 20.f;
    limited.max_jerk = 2000.f; // px/s³，明显限制
    limited.Apply(Vec2F{0.f, 0.f}, 0.008f);
    const Vec2F slow = limited.Apply(Vec2F{100.f, 0.f}, 0.008f);

    AIM_EXPECT_TRUE(std::fabs(slow.x) < std::fabs(fast.x));
}

// Reset 清空状态，下一帧重新直通
AIM_TEST(smoothing_reset_clears_state) {
    SmoothingFilter f;
    f.model = SmoothingModel::Critical;
    f.Apply(Vec2F{0.f, 0.f}, 0.008f);
    f.Apply(Vec2F{100.f, 0.f}, 0.008f);
    f.Reset();
    const Vec2F out = f.Apply(Vec2F{7.f, 3.f}, 0.008f);
    AIM_EXPECT_NEAR(out.x, 7.f, 1e-4f);
    AIM_EXPECT_NEAR(out.y, 3.f, 1e-4f);
}

int main() {
    return aim::test::run_all();
}
