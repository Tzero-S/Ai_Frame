#include "weapon/trigger_engine.h"
#include <algorithm>
#include <cmath>

namespace aim {

bool CrosshairHits(const Detection& d, float crosshair_x, float crosshair_y,
                   int aim_offset_px, float conf_threshold, float band_ratio) {
    if (d.score < conf_threshold) return false;

    const float x1 = std::min(d.box.x, d.box.right());
    const float x2 = std::max(d.box.x, d.box.right());
    const float y1 = std::min(d.box.y, d.box.bottom());
    const float y2 = std::max(d.box.y, d.box.bottom());

    if (crosshair_x < x1 || crosshair_x > x2) return false;

    const float line_y = (y1 + y2) * 0.5f + static_cast<float>(aim_offset_px);
    const float ratio = (band_ratio > 0.f) ? band_ratio : kHitBandRatio;
    const float band = std::max(1.f, (y2 - y1) * ratio);
    return std::fabs(crosshair_y - line_y) <= band;
}

void TriggerEngine::Configure(const TriggerParams& p) {
    p_ = p;
    Reset();
}

void TriggerEngine::Reset() {
    armed_ = false;
    firing_ = false;
    elapsed_ = 0.f;
    shot_elapsed_ = 0.f;
}

TriggerOutput TriggerEngine::Update(const TriggerInput& in, float dt_ms) {
    TriggerOutput out;

    // 未启用或未设置扳机键 → 整体不生效（§5.7.2 前置校验）
    if (!p_.enabled || p_.key.empty()) {
        if (firing_) { firing_ = false; out.fire_up = true; }
        armed_ = false;
        elapsed_ = 0.f;
        return out;
    }

    const bool want = in.key_down && in.crosshair_hit && in.target_score >= p_.conf_threshold;

    if (p_.type == TriggerType::Rifle) {
        if (want) {
            if (!armed_) { armed_ = true; elapsed_ = 0.f; }
            elapsed_ += dt_ms;
            if (!firing_ && elapsed_ >= static_cast<float>(p_.rifle_delay_ms)) {
                firing_ = true;
                out.fire_down = true;
            }
        } else {
            armed_ = false;
            elapsed_ = 0.f;
            if (firing_) { firing_ = false; out.fire_up = true; }
        }
    } else { // Sniper：单发点射
        if (want) {
            if (!armed_) { armed_ = true; elapsed_ = 0.f; }
            elapsed_ += dt_ms;
            if (!firing_ && elapsed_ >= static_cast<float>(p_.sniper_delay_ms)) {
                firing_ = true;
                shot_elapsed_ = 0.f;
                out.fire_down = true;
            }
            if (firing_) {
                shot_elapsed_ += dt_ms;
                if (shot_elapsed_ >= kSniperPulseMs) {
                    firing_ = false;
                    out.fire_up = true;
                    armed_ = false;
                    elapsed_ = 0.f;
                }
            }
        } else {
            armed_ = false;
            elapsed_ = 0.f;
            if (firing_) { firing_ = false; out.fire_up = true; }
        }
    }
    return out;
}

} // namespace aim
