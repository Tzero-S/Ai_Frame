#pragma once
#include <string>
#include "core/config.h"

// 键位绑定校验（§5.9.2）：同一键不允许绑定到两个互斥项；冲突时提示。
namespace aim {

struct BindingConflict {
    bool has = false;
    std::string key;
    std::string first;  // 冲突项 A
    std::string second; // 冲突项 B
};

// 收集功能热键 + 自瞄主/副键 + 压枪/扳机键，检测重复（空键忽略）。
BindingConflict FindBindingConflicts(const HotkeysConfig& hk,
                                     const AimConfig& aim,
                                     const WeaponConfig& weapon);

} // namespace aim
