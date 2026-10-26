#pragma once
#include <cstdint>
#include <vector>
#include "core/types.h"
#include "overlay/primitive_batch.h"

// 覆盖层内容构建（§5.11.3）：检测框 + 准星 + FOV 圈。
namespace aim::overlay {

struct OverlayStyle {
    uint32_t box_color = 0xFF00FF00; // 亮绿
    uint32_t aim_color = 0xFFFF0000; // 红
    uint32_t fov_color = 0xFF00BFFF; // 浅蓝
    float thickness = 1.5f;
    int circle_segments = 48;
};

PrimitiveBatch BuildOverlay(const std::vector<Detection>& dets,
                            float screen_w, float screen_h,
                            float fov_radius_px,
                            const OverlayStyle& style = {});

} // namespace aim::overlay
