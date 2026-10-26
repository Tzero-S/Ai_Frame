#include "aim_test.h"
#include "actuator/bezier.h"
#include "actuator/kmbox.h"
#include "actuator/ghub.h"
#include "actuator/sendinput.h"
#include <cmath>
#include <cstdlib>

using namespace aim;

AIM_TEST(bezier_strength0_is_linear) {
    BezierSpec spec; spec.type = BezierType::EaseIn; spec.strength = 0.f;
    auto ticks = BezierTicks(Vec2F{0, 0}, Vec2F{100, 0}, 4, spec);
    AIM_EXPECT_EQ(ticks.size(), 4u);
    for (const auto& t : ticks) {
        AIM_EXPECT_NEAR(t.x, 25.f, 1e-3f);
        AIM_EXPECT_NEAR(t.y, 0.f, 1e-4f);
    }
}

AIM_TEST(bezier_strength1_sums_and_varies) {
    BezierSpec spec; spec.type = BezierType::EaseIn; spec.strength = 1.f;
    auto ticks = BezierTicks(Vec2F{0, 0}, Vec2F{100, 0}, 4, spec);
    float sum = 0.f;
    for (const auto& t : ticks) sum += t.x;
    AIM_EXPECT_NEAR(sum, 100.f, 1e-3f);
    // 非匀速：首段与中段增量不同
    AIM_EXPECT_FALSE(std::fabs(ticks[0].x - ticks[1].x) < 1e-4f);
}

AIM_TEST(bezier_fractions_table) {
    float a = 0.f, b = 0.f;
    BezierFractions(BezierType::EaseIn, 0.f, a, b);
    AIM_EXPECT_NEAR(a, 1.f / 3.f, 1e-5f);
    AIM_EXPECT_NEAR(b, 2.f / 3.f, 1e-5f);

    BezierFractions(BezierType::EaseIn, 1.f, a, b);
    AIM_EXPECT_NEAR(a, 0.25f, 1e-5f);
    AIM_EXPECT_NEAR(b, 0.75f, 1e-5f);

    BezierFractions(BezierType::EaseOut, 1.f, a, b);
    AIM_EXPECT_NEAR(a, 0.75f, 1e-5f);
    AIM_EXPECT_NEAR(b, 0.25f, 1e-5f);

    BezierFractions(BezierType::Smoothing, 1.f, a, b);
    AIM_EXPECT_NEAR(a, 0.42f, 1e-5f);
    AIM_EXPECT_NEAR(b, 0.58f, 1e-5f);
}

AIM_TEST(kmbox_packet_layout) {
    std::array<uint8_t, 6> mac{1, 2, 3, 4, 5, 6};
    KmboxMove mv; mv.dx = 10; mv.dy = -20; mv.wheel = 0; mv.buttons = 1;
    auto pkt = BuildKmboxPacket(mac, 0x11223344u, 0x00A1u, KmboxCmd::Move, mv);

    AIM_EXPECT_EQ(pkt.size(), kKmboxPacketSize);
    for (int i = 0; i < 6; ++i) AIM_EXPECT_EQ(pkt[i], mac[i]);
    // rand 小端
    AIM_EXPECT_EQ(pkt[0x06], 0x44);
    AIM_EXPECT_EQ(pkt[0x07], 0x33);
    AIM_EXPECT_EQ(pkt[0x08], 0x22);
    AIM_EXPECT_EQ(pkt[0x09], 0x11);
    // index 小端
    AIM_EXPECT_EQ(pkt[0x0A], 0xA1);
    AIM_EXPECT_EQ(pkt[0x0B], 0x00);
    // cmd = 0xAEDE7345 小端 → 45 73 DE AE
    AIM_EXPECT_EQ(pkt[0x0C], 0x45);
    AIM_EXPECT_EQ(pkt[0x0D], 0x73);
    AIM_EXPECT_EQ(pkt[0x0E], 0xDE);
    AIM_EXPECT_EQ(pkt[0x0F], 0xAE);
    // mouse
    AIM_EXPECT_EQ(pkt[0x10], 1);
    AIM_EXPECT_EQ(pkt[0x14], 10);
    AIM_EXPECT_EQ(pkt[0x15], 0);
    AIM_EXPECT_EQ(pkt[0x18], 0xEC); // -20 的低字节
    AIM_EXPECT_EQ(pkt[0x19], 0xFF);
    AIM_EXPECT_EQ(pkt[0x1A], 0xFF);
    AIM_EXPECT_EQ(pkt[0x1B], 0xFF);
}

AIM_TEST(kmbox_parse_uid) {
    std::array<uint8_t, 4> uid{};
    AIM_EXPECT_TRUE(ParseUid("529C5C53", uid));
    AIM_EXPECT_EQ(uid[0], 0x52);
    AIM_EXPECT_EQ(uid[1], 0x9C);
    AIM_EXPECT_EQ(uid[2], 0x5C);
    AIM_EXPECT_EQ(uid[3], 0x53);
    AIM_EXPECT_FALSE(ParseUid("xyz", uid));
    AIM_EXPECT_FALSE(ParseUid("GGGGGGGG", uid));
}

AIM_TEST(ghub_split) {
    auto a = GhubSplit(300, 0, 127);
    AIM_EXPECT_EQ(a.size(), 3u);
    AIM_EXPECT_EQ(a[0].dx, 127);
    AIM_EXPECT_EQ(a[1].dx, 127);
    AIM_EXPECT_EQ(a[2].dx, 46);

    auto b = GhubSplit(50, -200, 127);
    AIM_EXPECT_EQ(b.size(), 2u);
    AIM_EXPECT_EQ(b[0].dx, 50);
    AIM_EXPECT_EQ(b[0].dy, -127);
    AIM_EXPECT_EQ(b[1].dx, 0);
    AIM_EXPECT_EQ(b[1].dy, -73);

    auto c = GhubSplit(0, 0, 127);
    AIM_EXPECT_EQ(c.size(), 0u);
}

AIM_TEST(sendinput_counts) {
    SendInputParams p; p.move_scale = 2.0f; p.min_step = 1;
    int dx = 0, dy = 0;
    ComputeMoveCounts(10.f, -3.f, p, dx, dy);
    AIM_EXPECT_EQ(dx, 20);
    AIM_EXPECT_EQ(dy, -6);

    SendInputParams q; q.move_scale = 1.0f; q.min_step = 2;
    ComputeMoveCounts(1.2f, 2.2f, q, dx, dy);
    AIM_EXPECT_EQ(dx, 0); // 1 < min_step 2 → 抖动过滤
    AIM_EXPECT_EQ(dy, 2);
}

int main() {
    return aim::test::run_all();
}
