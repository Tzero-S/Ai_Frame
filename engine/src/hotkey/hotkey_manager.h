#pragma once
#include "core/config.h"

// 瞄准激活状态机（§5.5.3）：
//   Hold  : 按下热键 A → 移动；松开 → 停
//   Toggle: 热键 A 边沿 → 在"锁定/解锁"间切换
//   Switch: 热键 A 边沿 → 在 Hold / Toggle 之间轮换（"自瞄切换 F4"）
// 两键状态独立记录，合并为 aim_armed = primary_state || secondary_state。
namespace aim {

class ActivationState {
public:
    void SetMode(ActivationMode m);
    ActivationMode ConfiguredMode() const { return configured_; }
    ActivationMode EffectiveMode() const { return effective_; }

    // 每帧调用；返回 aim_armed。
    bool Update(bool primary_down, bool secondary_down);

    bool Armed() const { return armed_; }
    void Reset();

private:
    ActivationMode configured_ = ActivationMode::Hold;
    ActivationMode effective_ = ActivationMode::Hold; // Switch 模式下的轮换结果
    bool prev_primary_ = false;
    bool toggled_ = false;
    bool armed_ = false;
};

} // namespace aim
