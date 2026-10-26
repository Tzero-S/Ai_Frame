#include "weapon/recoil_engine.h"
#include <algorithm>

namespace aim {

void RecoilEngine::Configure(const RecoilParams& p,
                             const PatternStore* store,
                             const std::string& pattern_id) {
    p_ = p;
    store_ = store;
    pattern_id_ = pattern_id;
    Reset();
}

float RecoilEngine::FireIntervalMs() const {
    return (p_.rpm > 0) ? (60000.f / static_cast<float>(p_.rpm)) : 0.f;
}

float RecoilEngine::BaseCompensation() const {
    // strength 0..30 线性映射：15 → 1.0 倍，30 → 2.0 倍
    const float scale = std::clamp(p_.strength, 0.f, 30.f) / 15.f;
    return std::max(0.f, p_.base_px) * scale;
}

Vec2F RecoilEngine::CumulativeAt(float t_ms) const {
    Vec2F out{};
    if (store_ && !pattern_id_.empty() && store_->Has(pattern_id_)) {
        // 曲线是归一化的累计形状（峰值 1.0），乘以峰值补偿得到像素
        const Vec2F shape = store_->Sample(pattern_id_, std::max(0.f, t_ms));
        const float peak = BaseCompensation();
        out = Vec2F{ peak * shape.x, peak * shape.y };
    } else {
        // 无弹道库：按发数线性下压，每发 strength*k 像素
        const float interval = FireIntervalMs();
        const float shots = (interval > 0.f) ? (std::max(0.f, t_ms) / interval) : 0.f;
        const float per_shot = std::clamp(p_.strength, 0.f, 30.f) * p_.k;
        out = Vec2F{ 0.f, per_shot * shots };
    }

    // 横向：手动模式下用垂直量 × 增益（覆盖曲线自带的 dx）
    if (p_.manual_horizontal) out.x = out.y * p_.horizontal_gain;
    return out;
}

Vec2F RecoilEngine::SampleNow() {
    const Vec2F cur = CumulativeAt(t_);
    if (!have_last_) {
        last_cum_ = Vec2F{};
        have_last_ = true;
    }
    Vec2F delta{ cur.x - last_cum_.x, cur.y - last_cum_.y };
    last_cum_ = cur;
    return delta;
}

} // namespace aim
