#include "aim/target_point.h"

namespace aim {

float PresetRatio(TargetPreset preset) {
    switch (preset) {
        case TargetPreset::Head:    return 0.12f;  // 框顶下方 12%
        case TargetPreset::Neck:    return 0.22f;
        case TargetPreset::Chest:   return 0.35f;
        case TargetPreset::Abdomen: return 0.50f;
        case TargetPreset::Legs:    return 0.78f;
        case TargetPreset::Custom:  return 0.12f;  // 由 offset_y_ratio 覆盖
    }
    return 0.12f;
}

PointF ComputeTargetPoint(const Detection& d,
                          TargetPreset preset,
                          float offset_y_ratio,
                          const AimConfig::MicroAdjust& ma) {
    const float r = (preset == TargetPreset::Custom) ? offset_y_ratio : PresetRatio(preset);
    PointF anchor{ d.box.cx(), d.box.y + r * d.box.h };

    // 四向微调（§5.5.2）：up=屏幕向上=负 y；left=负 x
    anchor.x += static_cast<float>(ma.right - ma.left);
    anchor.y += static_cast<float>(ma.down - ma.up);
    return anchor;
}

} // namespace aim
