#include "overlay/overlay_draw.h"

namespace aim::overlay {

PrimitiveBatch BuildOverlay(const std::vector<Detection>& dets,
                            float screen_w, float screen_h,
                            float fov_radius_px,
                            const OverlayStyle& style) {
    PrimitiveBatch b;

    for (const auto& d : dets) {
        b.AddRect(d.box, style.box_color, style.thickness);
    }

    const float cx = screen_w * 0.5f;
    const float cy = screen_h * 0.5f;
    b.AddCrosshair(cx, cy, 6.f, style.aim_color, style.thickness);

    if (fov_radius_px > 0.f) {
        b.AddCircle(cx, cy, fov_radius_px, style.fov_color, style.thickness,
                    style.circle_segments);
    }
    return b;
}

} // namespace aim::overlay
