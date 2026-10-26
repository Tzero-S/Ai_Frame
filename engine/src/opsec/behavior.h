#pragma once
#include <cstdint>
#include <random>
#include "core/config.h"
#include "core/types.h"

// L1 行为层（§5.12.1）：人类化移动、时机抖动、开火节奏拟真。
namespace aim::opsec {

struct ShapedMove {
    Vec2F delta;
    int duration_ms = 0;
};

class BehaviorShaper {
public:
    explicit BehaviorShaper(uint32_t seed = 0);

    void Configure(const AntiDetectConfig& cfg);
    ShapedMove ShapeMove(const Vec2F& delta, int duration_ms);
    int ShapeTriggerDelay(int base_ms);

    // 总开关：level=off 时全部退化为恒等
    bool Enabled() const { return cfg_.level != AntiDetectLevel::Off; }
    // 位置抖动与时机抖动相互独立：jitter_px=0 只关闭位置抖动，不影响时长抖动
    bool JitterEnabled() const { return Enabled() && cfg_.jitter_px > 0.f; }
    bool TimingEnabled() const { return Enabled() && cfg_.randomize_timing; }

private:
    AntiDetectConfig cfg_{};
    std::mt19937 rng_;
};

} // namespace aim::opsec
