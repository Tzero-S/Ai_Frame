#include "aim_test.h"
#include "core/config.h"
#include "core/config_store.h"
#include "opsec/anti_debug.h"
#include "opsec/behavior.h"
#include "opsec/network_policy.h"
#include "opsec/pacing.h"
#include "opsec/process_guard.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <string>

using namespace aim;

AIM_TEST(behavior_jitter_bounds_and_determinism) {
    AntiDetectConfig cfg;
    cfg.level = AntiDetectLevel::Standard;
    cfg.jitter_px = 1.0f;
    cfg.timing_jitter_ratio = 0.1f;

    opsec::BehaviorShaper a(42);
    opsec::BehaviorShaper b(42);
    a.Configure(cfg);
    b.Configure(cfg);

    for (int i = 0; i < 50; ++i) {
        const auto sa = a.ShapeMove(Vec2F{10.f, -5.f}, 100);
        const auto sb = b.ShapeMove(Vec2F{10.f, -5.f}, 100);

        AIM_EXPECT_NEAR(sa.delta.x, sb.delta.x, 1e-6f);
        AIM_EXPECT_NEAR(sa.delta.y, sb.delta.y, 1e-6f);
        AIM_EXPECT_EQ(sa.duration_ms, sb.duration_ms);

        AIM_EXPECT_TRUE(std::fabs(sa.delta.x - 10.f) <= 3.01f);
        AIM_EXPECT_TRUE(std::fabs(sa.delta.y + 5.f) <= 3.01f);
        AIM_EXPECT_TRUE(sa.duration_ms >= 90 && sa.duration_ms <= 110);
    }
}

AIM_TEST(behavior_disabled_is_identity) {
    AntiDetectConfig cfg;
    cfg.level = AntiDetectLevel::Off;

    opsec::BehaviorShaper s(7);
    s.Configure(cfg);
    const auto out = s.ShapeMove(Vec2F{3.f, 4.f}, 50);
    AIM_EXPECT_NEAR(out.delta.x, 3.f, 1e-6f);
    AIM_EXPECT_NEAR(out.delta.y, 4.f, 1e-6f);
    AIM_EXPECT_EQ(out.duration_ms, 50);
    AIM_EXPECT_EQ(s.ShapeTriggerDelay(70), 70);
}

AIM_TEST(behavior_trigger_delay_bounds) {
    AntiDetectConfig cfg;
    cfg.level = AntiDetectLevel::Standard;
    cfg.randomize_timing = true;

    opsec::BehaviorShaper s(9);
    s.Configure(cfg);
    for (int i = 0; i < 100; ++i) {
        const int d = s.ShapeTriggerDelay(70);
        AIM_EXPECT_TRUE(d >= 59 && d <= 81); // 70ms ±15%
    }
}

// 位置抖动与时机抖动解耦：jitter_px=0 只关闭位置抖动，时长抖动仍生效
AIM_TEST(behavior_jitter_zero_keeps_timing) {
    AntiDetectConfig cfg;
    cfg.level = AntiDetectLevel::Standard;
    cfg.jitter_px = 0.f;
    cfg.timing_jitter_ratio = 0.2f;
    cfg.randomize_timing = true;

    opsec::BehaviorShaper s(7);
    s.Configure(cfg);
    AIM_EXPECT_FALSE(s.JitterEnabled());
    AIM_EXPECT_TRUE(s.TimingEnabled());

    bool delta_changed = false;
    bool duration_changed = false;
    for (int i = 0; i < 50; ++i) {
        const auto out = s.ShapeMove(Vec2F{10.f, 5.f}, 100);
        if (out.delta.x != 10.f || out.delta.y != 5.f) delta_changed = true;
        if (out.duration_ms != 100) duration_changed = true;
    }
    AIM_EXPECT_FALSE(delta_changed);
    AIM_EXPECT_TRUE(duration_changed);
}

// 反向解耦：randomize_timing=false 时只抖位置、不抖时长
AIM_TEST(behavior_timing_off_keeps_duration) {
    AntiDetectConfig cfg;
    cfg.level = AntiDetectLevel::Standard;
    cfg.jitter_px = 1.0f;
    cfg.randomize_timing = false;

    opsec::BehaviorShaper s(8);
    s.Configure(cfg);
    AIM_EXPECT_TRUE(s.JitterEnabled());
    AIM_EXPECT_FALSE(s.TimingEnabled());

    bool delta_changed = false;
    bool duration_changed = false;
    for (int i = 0; i < 50; ++i) {
        const auto out = s.ShapeMove(Vec2F{10.f, 5.f}, 100);
        if (out.delta.x != 10.f || out.delta.y != 5.f) delta_changed = true;
        if (out.duration_ms != 100) duration_changed = true;
    }
    AIM_EXPECT_TRUE(delta_changed);
    AIM_EXPECT_FALSE(duration_changed);
}

AIM_TEST(hysteresis_requires_consecutive_frames) {
    opsec::TargetHysteresis h;
    h.Configure(2);

    AIM_EXPECT_FALSE(h.AllowSwitch(1, 0)); // 第 1 帧
    AIM_EXPECT_TRUE(h.AllowSwitch(1, 0));  // 第 2 帧

    h.Reset();
    AIM_EXPECT_FALSE(h.AllowSwitch(2, 0));
    AIM_EXPECT_TRUE(h.AllowSwitch(0, 0));  // 同目标直接允许
    AIM_EXPECT_FALSE(h.AllowSwitch(2, 0)); // 候选被重置
}

AIM_TEST(idle_pacer_throttles) {
    opsec::IdlePacer p;
    p.Configure(120, 30, 10);

    AIM_EXPECT_EQ(p.NextDelayMs(true), 0);
    for (int i = 0; i < 9; ++i) AIM_EXPECT_EQ(p.NextDelayMs(false), 0);
    AIM_EXPECT_EQ(p.NextDelayMs(false), 33); // 第 10 帧起降频
    AIM_EXPECT_EQ(p.NextDelayMs(true), 0);   // 恢复
}

AIM_TEST(process_title_match) {
    AIM_EXPECT_TRUE(opsec::ProcessGuard::TitleMatches("VALORANT  - 123", "valorant"));
    AIM_EXPECT_FALSE(opsec::ProcessGuard::TitleMatches("Notepad", "valorant"));
    AIM_EXPECT_TRUE(opsec::ProcessGuard::TitleMatches("anything", ""));
}

AIM_TEST(process_focus_pause) {
    opsec::ProcessGuard g;
    g.Configure(true, "AIM_NO_SUCH_GAME_WINDOW_XYZ");
    AIM_EXPECT_TRUE(g.ShouldPause()); // 前台不是该游戏

    g.Configure(false, "AIM_NO_SUCH_GAME_WINDOW_XYZ");
    AIM_EXPECT_FALSE(g.ShouldPause());
}

AIM_TEST(process_secure_zero) {
    unsigned char buf[64];
    std::memset(buf, 0xAB, sizeof(buf));
    opsec::ProcessGuard::SecureZero(buf, sizeof(buf));
    for (unsigned char c : buf) AIM_EXPECT_EQ(static_cast<int>(c), 0);
}

AIM_TEST(process_tool_window_flag) {
    HWND h = CreateWindowExA(0, "STATIC", "aim-test", WS_OVERLAPPED,
                             0, 0, 10, 10, nullptr, nullptr, nullptr, nullptr);
    AIM_EXPECT_TRUE(h != nullptr);
    if (!h) return;

    opsec::ProcessGuard::ApplyToolWindow(h);
    const LONG_PTR style = GetWindowLongPtrA(h, GWL_EXSTYLE);
    AIM_EXPECT_TRUE((style & WS_EX_TOOLWINDOW) != 0);
    AIM_EXPECT_TRUE((style & WS_EX_APPWINDOW) == 0);
    DestroyWindow(h);
}

AIM_TEST(network_policy_decisions) {
    opsec::NetworkPolicy p;
    p.min_tls_version = 13;
    p.pinned_sha256 = {"abc"};
    p.require_signed_requests = true;

    AIM_EXPECT_TRUE(opsec::EvaluatePeer(p, {12, "abc"}, true) ==
                    opsec::NetworkDecision::RejectTlsVersion);
    AIM_EXPECT_TRUE(opsec::EvaluatePeer(p, {13, "zzz"}, true) ==
                    opsec::NetworkDecision::RejectPin);
    AIM_EXPECT_TRUE(opsec::EvaluatePeer(p, {13, "abc"}, false) ==
                    opsec::NetworkDecision::RejectSignature);
    AIM_EXPECT_TRUE(opsec::EvaluatePeer(p, {13, "abc"}, true) ==
                    opsec::NetworkDecision::Allow);
}

AIM_TEST(anti_debug_default_off) {
    AIM_EXPECT_FALSE(opsec::AntiDebugGate(false));
    AIM_EXPECT_EQ(opsec::AntiDebugGate(true), opsec::IsDebuggerAttached());
}

AIM_TEST(anti_detect_config_roundtrip) {
    Config d = ConfigManager::Defaults();
    AIM_EXPECT_NEAR(d.anti_detect.jitter_px, 0.8f, 1e-5f);
    AIM_EXPECT_EQ(d.anti_detect.switch_hysteresis_frames, 2);
    AIM_EXPECT_FALSE(d.anti_detect.anti_debug);

    d.anti_detect.jitter_px = 1.5f;
    d.anti_detect.idle_fps = 60;

    const std::string js = ConfigManager::ToJson(d, 0);
    auto r = ConfigManager::FromJson(js);
    AIM_EXPECT_TRUE(r.ok());
    if (r.ok()) {
        AIM_EXPECT_NEAR(r.value().anti_detect.jitter_px, 1.5f, 1e-5f);
        AIM_EXPECT_EQ(r.value().anti_detect.idle_fps, 60);
    }
}

int main() {
    return aim::test::run_all();
}
