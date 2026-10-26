#include "aim_test.h"
#include "aim/lead.h"

using namespace aim;

AIM_TEST(lead_linear_horizontal) {
    LeadPredictor lp;
    lp.AddSample(0.0, Vec2F{0, 0});
    lp.AddSample(10.0, Vec2F{10, 0});
    lp.AddSample(20.0, Vec2F{20, 0});

    const Vec2F v = lp.Velocity();
    AIM_EXPECT_NEAR(v.x, 1.f, 1e-4f);
    AIM_EXPECT_NEAR(v.y, 0.f, 1e-4f);

    const Vec2F p = lp.Predict(10.0, 1.f);
    AIM_EXPECT_NEAR(p.x, 30.f, 1e-3f); // p_now(20) + 1*10
    AIM_EXPECT_NEAR(p.y, 0.f, 1e-3f);

    const Vec2F p2 = lp.Predict(10.0, 0.5f);
    AIM_EXPECT_NEAR(p2.x, 25.f, 1e-3f);
}

AIM_TEST(lead_vertical_negative) {
    LeadPredictor lp;
    lp.AddSample(0.0, Vec2F{0, 0});
    lp.AddSample(10.0, Vec2F{0, -5});

    AIM_EXPECT_NEAR(lp.Velocity().y, -0.5f, 1e-4f);
    const Vec2F p = lp.Predict(10.0, 1.f);
    AIM_EXPECT_NEAR(p.y, -10.f, 1e-3f); // -5 + (-0.5*10)
}

AIM_TEST(lead_insufficient_samples) {
    LeadPredictor lp;
    AIM_EXPECT_NEAR(lp.Predict(10.0, 1.f).x, 0.f, 1e-6f);

    lp.AddSample(0.0, Vec2F{5, 6});
    const Vec2F p = lp.Predict(10.0, 1.f);
    AIM_EXPECT_NEAR(p.x, 5.f, 1e-6f);
    AIM_EXPECT_NEAR(p.y, 6.f, 1e-6f);
}

AIM_TEST(lead_zero_latency_or_gain) {
    LeadPredictor lp;
    lp.AddSample(0.0, Vec2F{0, 0});
    lp.AddSample(10.0, Vec2F{10, 0});

    AIM_EXPECT_NEAR(lp.Predict(0.0, 1.f).x, 10.f, 1e-6f);
    AIM_EXPECT_NEAR(lp.Predict(10.0, 0.f).x, 10.f, 1e-6f);
}

AIM_TEST(lead_constant_time_no_velocity) {
    LeadPredictor lp;
    lp.AddSample(5.0, Vec2F{0, 0});
    lp.AddSample(5.0, Vec2F{10, 0});

    AIM_EXPECT_NEAR(lp.Velocity().x, 0.f, 1e-6f);
    AIM_EXPECT_NEAR(lp.Predict(10.0, 1.f).x, 10.f, 1e-6f);
}

AIM_TEST(lead_window_limits) {
    LeadPredictor lp(3);
    for (int i = 0; i < 10; ++i) {
        lp.AddSample(i * 10.0, Vec2F{static_cast<float>(i), 0.f});
    }
    // 窗口只保留最后 3 个：t=70/80/90, p=7/8/9 → v=0.1
    AIM_EXPECT_EQ(lp.Count(), 3u);
    AIM_EXPECT_NEAR(lp.Velocity().x, 0.1f, 1e-4f);
}

int main() {
    return aim::test::run_all();
}
