#pragma once
#include "core/types.h"
#include "core/config.h"

// 瞄准部位锚点（§5.5.2）。返回目标框内的相对锚点（屏幕坐标），
// 并由四向微调叠加静态像素偏移。
namespace aim {

// r：部位在框内的纵向比例（§5.5.2 表）。
float PresetRatio(TargetPreset preset);

// anchor = (cx, y1 + r*h)，再叠加 micro_adjust（up:屏幕向上=负y；left:负x）。
PointF ComputeTargetPoint(const Detection& d,
                          TargetPreset preset,
                          float offset_y_ratio,
                          const AimConfig::MicroAdjust& ma);

} // namespace aim
