#include "hotkey/keybinding.h"
#include <vector>
#include <utility>

namespace aim {

BindingConflict FindBindingConflicts(const HotkeysConfig& hk,
                                     const AimConfig& aim,
                                     const WeaponConfig& weapon) {
    std::vector<std::pair<std::string, std::string>> items = {
        {"hotkeys.toggle_hud", hk.toggle_hud},
        {"hotkeys.toggle_team", hk.toggle_team},
        {"hotkeys.toggle_aim", hk.toggle_aim},
        {"hotkeys.adhesion_minus", hk.adhesion_minus},
        {"hotkeys.adhesion_plus", hk.adhesion_plus},
        {"hotkeys.push_switch", hk.push_switch},
        {"hotkeys.target_up", hk.target_up},
        {"hotkeys.target_down", hk.target_down},
        {"hotkeys.target_left", hk.target_left},
        {"hotkeys.target_right", hk.target_right},
        {"aim.hotkey_primary", aim.hotkey_primary},
        {"aim.hotkey_secondary", aim.hotkey_secondary},
        {"weapon.recoil.hotkey", weapon.recoil.hotkey},
        {"weapon.trigger.key", weapon.trigger.key},
    };

    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].second.empty()) continue; // 未设置不参与冲突
        for (size_t j = i + 1; j < items.size(); ++j) {
            if (items[j].second.empty()) continue;
            if (items[i].second == items[j].second) {
                BindingConflict c;
                c.has = true;
                c.key = items[i].second;
                c.first = items[i].first;
                c.second = items[j].first;
                return c;
            }
        }
    }
    return BindingConflict{};
}

} // namespace aim
