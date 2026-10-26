#include "opsec/behavior.h"

#include <algorithm>
#include <cmath>

namespace aim::opsec {

namespace {
// §5.12.1：扳机延时默认 70ms ±15%。该比例与"移动步长/间隔"的 timing_jitter_ratio
// 是两个独立口径（后者默认 ±10%），因此不共用配置字段。
constexpr float kTriggerDelayJitterRatio = 0.15f;
} // namespace

BehaviorShaper::BehaviorShaper(uint32_t seed)
    : rng_(seed ? seed : static_cast<uint32_t>(std::random_device{}())) {}

void BehaviorShaper::Configure(const AntiDetectConfig& cfg) { cfg_ = cfg; }

ShapedMove BehaviorShaper::ShapeMove(const Vec2F& delta, int duration_ms) {
    ShapedMove out{ delta, duration_ms };
    if (!Enabled()) return out;

    // 位置抖动（σ 高斯，截断到 ±3σ）
    if (JitterEnabled()) {
        const float sigma = std::clamp(cfg_.jitter_px, 0.f, 5.f);
        std::normal_distribution<float> jitter(0.f, sigma);
        out.delta.x += std::clamp(jitter(rng_), -3.f * sigma, 3.f * sigma);
        out.delta.y += std::clamp(jitter(rng_), -3.f * sigma, 3.f * sigma);
    }

    // 时机抖动（时长 ±ratio），与位置抖动解耦
    if (TimingEnabled()) {
        const float ratio = std::clamp(cfg_.timing_jitter_ratio, 0.f, 0.5f);
        std::uniform_real_distribution<float> timing(-ratio, ratio);
        out.duration_ms = std::max(
            0, static_cast<int>(std::lround(duration_ms * (1.f + timing(rng_)))));
    }
    return out;
}

int BehaviorShaper::ShapeTriggerDelay(int base_ms) {
    if (!TimingEnabled()) return base_ms;

    std::uniform_real_distribution<float> timing(-kTriggerDelayJitterRatio,
                                                 kTriggerDelayJitterRatio);
    return std::max(0, static_cast<int>(std::lround(base_ms * (1.f + timing(rng_)))));
}

} // namespace aim::opsec
