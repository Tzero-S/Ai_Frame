#include "aim/aim_calc.h"

namespace aim {

AimResult ComputeAim(const Detection& target,
                     const AimConfig& aim,
                     float screen_w, float screen_h,
                     const Fov& fov,
                     const Vec2F& recoil_comp_px) {
    const PointF anchor = ComputeTargetPoint(
        target, aim.target_point.preset, aim.target_point.offset_y_ratio, aim.micro_adjust);

    // 准星基准点 = 屏幕几何中心 + 每游戏偏移（§5.5.1）
    const float cx = screen_w * 0.5f + aim.crosshair_offset_x;
    const float cy = screen_h * 0.5f + aim.crosshair_offset_y;

    AimResult r;
    r.delta_px.x = (anchor.x - cx) + recoil_comp_px.x;
    r.delta_px.y = (anchor.y - cy) + recoil_comp_px.y;

    // §7.1 像素 → 角度
    r.delta_angle_deg.x = Rad2Deg(AngleX(r.delta_px.x, fov.FocalX(screen_w)));
    r.delta_angle_deg.y = Rad2Deg(AngleY(r.delta_px.y, fov.FocalY(screen_h)));
    return r;
}

} // namespace aim
