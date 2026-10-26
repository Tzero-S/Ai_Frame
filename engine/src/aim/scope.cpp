#include "aim/scope.h"
#include <cmath>

namespace aim {

bool InScope(const Detection& d, const ScopeSpec& s,
             float screen_w, float screen_h, const Fov& fov,
             const Vec2F* center) {
    const PointF p = d.aim_point();
    const float cx = center ? center->x : screen_w * 0.5f;
    const float cy = center ? center->y : screen_h * 0.5f;
    const float dx = p.x - cx;
    const float dy = p.y - cy;

    switch (s.mode) {
        case ScopeMode::Rect:
            return std::fabs(dx) <= s.half_w && std::fabs(dy) <= s.half_h;

        case ScopeMode::Circle:
            return (dx * dx + dy * dy) <= (s.radius_px * s.radius_px);

        case ScopeMode::Fov: {
            // §5.5.1 角度制：由 HFOV/VFOV 推出的半角比较
            const float ax = Rad2Deg(AngleX(dx, fov.FocalX(screen_w)));
            const float ay = Rad2Deg(AngleY(dy, fov.FocalY(screen_h)));
            return std::fabs(ax) <= s.hfov_half_deg && std::fabs(ay) <= s.vfov_half_deg;
        }
    }
    return true;
}

} // namespace aim
