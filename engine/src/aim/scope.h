#pragma once
#include "core/types.h"
#include "core/config.h"
#include "fov/fov_math.h"

// 瞄准范围过滤（§5.5.1）。检测框须已映射到屏幕坐标（后处理完成坐标反变换）。
namespace aim {

struct ScopeSpec {
    ScopeMode mode = ScopeMode::Rect;
    float half_w = 320.f;
    float half_h = 240.f;
    float radius_px = 0.f;
    float hfov_half_deg = 0.f;
    float vfov_half_deg = 0.f;
};

// 返回检测 d 是否落在瞄准范围内。
// center 为空时取屏幕几何中心；跨游戏适配时可传入"准星实际位置"（§5.5.1）。
bool InScope(const Detection& d, const ScopeSpec& s,
             float screen_w, float screen_h, const Fov& fov,
             const Vec2F* center = nullptr);

} // namespace aim
