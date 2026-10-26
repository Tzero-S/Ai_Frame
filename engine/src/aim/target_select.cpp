#include "aim/target_select.h"
#include <cmath>
#include <limits>

namespace aim {

const Detection* SelectTarget(const std::vector<Detection>& dets,
                              TargetSelection mode,
                              const ScopeSpec& scope,
                              float screen_w, float screen_h,
                              const Fov& fov,
                              const Detection* locked,
                              const Vec2F* center) {
    const float cx = center ? center->x : screen_w * 0.5f;
    const float cy = center ? center->y : screen_h * 0.5f;

    // 1) 范围过滤（§5.5.1）
    std::vector<const Detection*> candidates;
    candidates.reserve(dets.size());
    for (const auto& d : dets) {
        if (InScope(d, scope, screen_w, screen_h, fov, center)) candidates.push_back(&d);
    }
    if (candidates.empty()) return nullptr;

    auto dist2 = [&](const Detection* d) {
        const PointF p = d->aim_point();
        const float dx = p.x - cx;
        const float dy = p.y - cy;
        return dx * dx + dy * dy;
    };

    // 2) 选择策略（§5.5.1）
    switch (mode) {
        case TargetSelection::HighestScore: {
            const Detection* best = candidates.front();
            for (const Detection* d : candidates)
                if (d->score > best->score) best = d;
            return best;
        }
        case TargetSelection::Sticky: {
            if (locked) {
                for (const Detection* d : candidates)
                    if (d == locked) return locked; // 仍锁定
            }
            // 无锁定或已丢失 → 退回最近准星
            const Detection* best = candidates.front();
            float best_d2 = dist2(best);
            for (const Detection* d : candidates) {
                float d2 = dist2(d);
                if (d2 < best_d2) { best_d2 = d2; best = d; }
            }
            return best;
        }
        case TargetSelection::NearestCrosshair:
        default: {
            const Detection* best = candidates.front();
            float best_d2 = dist2(best);
            for (const Detection* d : candidates) {
                float d2 = dist2(d);
                if (d2 < best_d2) { best_d2 = d2; best = d; }
            }
            return best;
        }
    }
}

} // namespace aim
