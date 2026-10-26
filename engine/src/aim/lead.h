#pragma once
#include <cstddef>
#include <deque>
#include "core/types.h"

// 推进/提前量（§7.2）：对最近 K 帧瞄准点做最小二乘拟合求速度，
// predicted = p_now + v * (latency_ms * lead_gain)。
// 单位：t 为毫秒，v 为 px/ms，latency 为毫秒。
namespace aim {

class LeadPredictor {
public:
    explicit LeadPredictor(size_t window = 8) : window_(window ? window : 1) {}

    void SetWindow(size_t k);
    void AddSample(double t_ms, const Vec2F& p);
    Vec2F Velocity() const;
    Vec2F Predict(double latency_ms, float lead_gain) const;
    void Reset() { samples_.clear(); }
    size_t Count() const { return samples_.size(); }

private:
    struct Sample {
        double t = 0.0;
        Vec2F p{};
    };
    std::deque<Sample> samples_;
    size_t window_ = 8;
};

} // namespace aim
