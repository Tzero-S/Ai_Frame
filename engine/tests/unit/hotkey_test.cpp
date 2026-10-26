#include "aim_test.h"
#include "hotkey/hotkey_manager.h"
#include "hotkey/keybinding.h"

using namespace aim;

AIM_TEST(activation_hold_mode) {
    ActivationState s;
    s.SetMode(ActivationMode::Hold);
    AIM_EXPECT_TRUE(s.Update(true, false));   // 按下 → armed
    AIM_EXPECT_TRUE(s.Armed());
    AIM_EXPECT_FALSE(s.Update(false, false)); // 松开 → 停
    AIM_EXPECT_TRUE(s.Update(false, true));   // 副键独立触发
}

AIM_TEST(activation_toggle_mode) {
    ActivationState s;
    s.SetMode(ActivationMode::Toggle);
    AIM_EXPECT_TRUE(s.Update(true, false));   // 边沿 → 锁定
    AIM_EXPECT_TRUE(s.Update(false, false));  // 松开仍锁定
    AIM_EXPECT_FALSE(s.Update(true, false));  // 再次边沿 → 解锁
    AIM_EXPECT_FALSE(s.Update(false, false));
}

AIM_TEST(activation_switch_mode) {
    ActivationState s;
    s.SetMode(ActivationMode::Switch);
    AIM_EXPECT_EQ(s.EffectiveMode(), ActivationMode::Hold);

    AIM_EXPECT_TRUE(s.Update(true, false));   // 边沿 → 切到 Toggle 并锁定
    AIM_EXPECT_EQ(s.EffectiveMode(), ActivationMode::Toggle);
    AIM_EXPECT_TRUE(s.Update(false, false));  // 保持锁定

    AIM_EXPECT_TRUE(s.Update(true, false));   // 边沿 → 切回 Hold（此时按住 → armed）
    AIM_EXPECT_EQ(s.EffectiveMode(), ActivationMode::Hold);
    AIM_EXPECT_FALSE(s.Update(false, false)); // 松开 → 停
}

AIM_TEST(binding_conflicts_default_none) {
    Config d = ConfigManager::Defaults();
    BindingConflict c = FindBindingConflicts(d.hotkeys, d.aim, d.weapon);
    AIM_EXPECT_FALSE(c.has);
}

AIM_TEST(binding_conflicts_detected) {
    Config d = ConfigManager::Defaults();
    d.hotkeys.toggle_hud = "F4"; // 与 toggle_aim 冲突
    BindingConflict c = FindBindingConflicts(d.hotkeys, d.aim, d.weapon);
    AIM_EXPECT_TRUE(c.has);
    AIM_EXPECT_STREQ(c.key, "F4");

    Config e = ConfigManager::Defaults();
    e.aim.hotkey_primary = "F9"; // 与 toggle_hud 冲突
    BindingConflict c2 = FindBindingConflicts(e.hotkeys, e.aim, e.weapon);
    AIM_EXPECT_TRUE(c2.has);
    AIM_EXPECT_STREQ(c2.key, "F9");
}

AIM_TEST(binding_empty_keys_ignored) {
    Config d = ConfigManager::Defaults();
    d.weapon.trigger.key = "";
    d.weapon.recoil.hotkey = "";
    BindingConflict c = FindBindingConflicts(d.hotkeys, d.aim, d.weapon);
    AIM_EXPECT_FALSE(c.has);
}

int main() {
    return aim::test::run_all();
}
