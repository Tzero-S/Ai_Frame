#pragma once
#include "core/types.h"
#include "core/config.h"

// 弹道微调（§5.5.4）：recoil_micro 为静态补偿（垂直/水平像素偏置），
// 由 AimCalc 以 recoil_comp_px 注入；动态弹道 pattern 由 weapon 模块采样。
namespace aim {

inline Vec2F RecoilMicroCompensation(const AimConfig::RecoilMicro& rm) {
    if (!rm.enabled) return Vec2F{};
    return Vec2F{ rm.horizontal, rm.vertical }; // 正=右/下
}

} // namespace aim
