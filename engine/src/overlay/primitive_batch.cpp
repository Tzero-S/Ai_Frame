#include "overlay/primitive_batch.h"
#include <algorithm>
#include <cmath>

namespace aim::overlay {

void PrimitiveBatch::AddTriangle(const OverlayVertex& a, const OverlayVertex& b,
                                 const OverlayVertex& c) {
    vertices_.push_back(a);
    vertices_.push_back(b);
    vertices_.push_back(c);
}

void PrimitiveBatch::AddLine(float x1, float y1, float x2, float y2,
                             uint32_t color, float thickness) {
    const float t = std::max(thickness, 0.5f);
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) { // 退化成一个点，给一个最小方向
        dx = 1.f;
        dy = 0.f;
        len = 1.f;
    }
    const float nx = -dy / len * (t * 0.5f);
    const float ny = dx / len * (t * 0.5f);

    const OverlayVertex a{ x1 + nx, y1 + ny, color };
    const OverlayVertex b{ x2 + nx, y2 + ny, color };
    const OverlayVertex c{ x2 - nx, y2 - ny, color };
    const OverlayVertex d{ x1 - nx, y1 - ny, color };

    AddTriangle(a, b, c);
    AddTriangle(a, c, d);
}

void PrimitiveBatch::AddRect(const RectF& r, uint32_t color, float thickness) {
    const float x1 = r.x;
    const float y1 = r.y;
    const float x2 = r.x + r.w;
    const float y2 = r.y + r.h;
    AddLine(x1, y1, x2, y1, color, thickness); // 上
    AddLine(x2, y1, x2, y2, color, thickness); // 右
    AddLine(x2, y2, x1, y2, color, thickness); // 下
    AddLine(x1, y2, x1, y1, color, thickness); // 左
}

void PrimitiveBatch::AddCircle(float cx, float cy, float radius,
                               uint32_t color, float thickness, int segments) {
    if (radius <= 0.f) return;
    const int n = std::max(segments, 3);
    float px = cx + radius;
    float py = cy;
    for (int i = 1; i <= n; ++i) {
        const float a = 6.28318530718f * static_cast<float>(i) / static_cast<float>(n);
        const float x = cx + radius * std::cos(a);
        const float y = cy + radius * std::sin(a);
        AddLine(px, py, x, y, color, thickness);
        px = x;
        py = y;
    }
}

void PrimitiveBatch::AddCrosshair(float cx, float cy, float size,
                                  uint32_t color, float thickness) {
    AddLine(cx - size, cy, cx + size, cy, color, thickness);
    AddLine(cx, cy - size, cx, cy + size, color, thickness);
}

} // namespace aim::overlay
