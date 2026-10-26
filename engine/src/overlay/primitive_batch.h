#pragma once
#include <cstdint>
#include <vector>
#include "core/types.h"

// 覆盖层图元批处理（§5.11.3 / §9.1 primitive_batch）：
// 把框/线/圆/准星转成三角形列表，供 2D/3D 渲染器统一提交。
// 颜色格式 0xAARRGGBB。
namespace aim::overlay {

struct OverlayVertex {
    float x = 0.f;
    float y = 0.f;
    uint32_t color = 0xFFFFFFFF;
};

class PrimitiveBatch {
public:
    void Clear() { vertices_.clear(); }

    // 线段（以 thickness 展开为四边形 → 2 三角形）
    void AddLine(float x1, float y1, float x2, float y2,
                 uint32_t color, float thickness = 1.f);

    // 矩形框（4 条边）
    void AddRect(const RectF& r, uint32_t color, float thickness = 1.f);

    // 圆（segments 段折线）
    void AddCircle(float cx, float cy, float radius,
                   uint32_t color, float thickness = 1.f, int segments = 48);

    // 准星（水平 + 垂直两条线）
    void AddCrosshair(float cx, float cy, float size,
                      uint32_t color, float thickness = 1.f);

    const std::vector<OverlayVertex>& vertices() const { return vertices_; }
    size_t vertex_count() const { return vertices_.size(); }
    bool empty() const { return vertices_.empty(); }

private:
    void AddTriangle(const OverlayVertex& a, const OverlayVertex& b, const OverlayVertex& c);

    std::vector<OverlayVertex> vertices_;
};

} // namespace aim::overlay
