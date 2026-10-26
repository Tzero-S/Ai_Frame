#pragma once
#include "core/types.h"
#include "core/config.h"
#include "aim/target_point.h"
#include "fov/fov_math.h"

// 瞄准计算（§5.5.4）：几何瞄准量 + 弹道补偿量 + 左右微调量。
// 输出像素位移与角度位移；弹道补偿由 weapon 模块以 recoil_comp_px 注入。
namespace aim {

struct AimResult {
    Vec2F delta_px;        // 相对屏幕中心的像素位移（已含微调与补偿）
    Vec2F delta_angle_deg; // 对应角度位移（度）
};

// target: 已映射到屏幕坐标的检测框。
// recoil_comp_px: 压枪/弹道补偿（像素，正=向右/向下），无补偿传 {0,0}。
AimResult ComputeAim(const Detection& target,
                     const AimConfig& aim,
                     float screen_w, float screen_h,
                     const Fov& fov,
                     const Vec2F& recoil_comp_px = {});

} // namespace aim
