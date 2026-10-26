#include "overlay/overlay_preview.h"

namespace aim::overlay {

bool OverlayPreview::init(int width, int height, bool click_through, std::string* err) {
    return window_.init(width, height, click_through, err);
}

void OverlayPreview::shutdown() { window_.shutdown(); }

void OverlayPreview::set_visible(bool v) { window_.set_visible(v); }

void OverlayPreview::render(const std::vector<Detection>& dets,
                            float screen_w, float screen_h, float fov_radius_px) {
    const PrimitiveBatch batch =
        BuildOverlay(dets, screen_w, screen_h, fov_radius_px, style_);
    window_.present(batch);
}

} // namespace aim::overlay
