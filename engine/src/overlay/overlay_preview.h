#pragma once
#include <string>
#include <vector>
#include "core/types.h"
#include "overlay/overlay_draw.h"
#include "overlay/overlay_window.h"

// 覆盖层实时预览（§5.11.3）：检测框 + 准星 + FOV 圈 → 分层窗口。
namespace aim::overlay {

class OverlayPreview {
public:
    bool init(int width, int height, bool click_through = true, std::string* err = nullptr);
    void shutdown();
    void set_visible(bool v);

    void render(const std::vector<Detection>& dets,
                float screen_w, float screen_h, float fov_radius_px);

    int present_count() const { return window_.present_count(); }
    int last_vertex_count() const { return window_.last_vertex_count(); }
    const uint32_t* pixels() const { return window_.pixels(); }
    void* handle() const { return window_.handle(); }

private:
    OverlayWindow window_;
    OverlayStyle style_{};
};

} // namespace aim::overlay
