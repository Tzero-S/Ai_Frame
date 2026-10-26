#include "aim_test.h"
#include "hotkey/key_state.h"

using namespace aim;

// 键名 → 虚拟键码（§5.9.1 绑定口径）
AIM_TEST(key_name_to_vk) {
    AIM_EXPECT_EQ(VkFromName("MOUSE1"), 0x01);
    AIM_EXPECT_EQ(VkFromName("MOUSE2"), 0x02);
    AIM_EXPECT_EQ(VkFromName("MOUSE4"), 0x05);
    AIM_EXPECT_EQ(VkFromName("F4"), 0x73);
    AIM_EXPECT_EQ(VkFromName("F12"), 0x7B);
    AIM_EXPECT_EQ(VkFromName("NUMPAD8"), 0x68);
    AIM_EXPECT_EQ(VkFromName("UP"), 0x26);
    AIM_EXPECT_EQ(VkFromName("LEFT"), 0x25);
    AIM_EXPECT_EQ(VkFromName("A"), 0x41);
    AIM_EXPECT_EQ(VkFromName("5"), 0x35);
    AIM_EXPECT_EQ(VkFromName("SPACE"), 0x20);
}

// 大小写不敏感；未知键/空键返回 0（表示未绑定）
AIM_TEST(key_name_case_and_unknown) {
    AIM_EXPECT_EQ(VkFromName("mouse2"), 0x02);
    AIM_EXPECT_EQ(VkFromName("f9"), 0x78);
    AIM_EXPECT_EQ(VkFromName(""), 0);
    AIM_EXPECT_EQ(VkFromName("NOT_A_KEY"), 0);
    AIM_EXPECT_EQ(VkFromName("F99"), 0);
    AIM_EXPECT_EQ(VkFromName("NUMPADX"), 0);
}

// 边沿检测：只在"抬起→按下"那一帧为真；持续按住不重复触发
AIM_TEST(key_edge_detects_press_once) {
    KeyEdge e;
    AIM_EXPECT_TRUE(e.PressedState(0x02, true));   // 首帧按下 → 触发
    AIM_EXPECT_FALSE(e.PressedState(0x02, true));  // 持续按住 → 不重复
    AIM_EXPECT_FALSE(e.PressedState(0x02, false)); // 抬起
    AIM_EXPECT_TRUE(e.PressedState(0x02, true));   // 再按 → 再触发

    // 不同键互不影响
    AIM_EXPECT_TRUE(e.PressedState(0x73, true));
    AIM_EXPECT_FALSE(e.PressedState(0x02, true));

    // 非法键码不触发
    AIM_EXPECT_FALSE(e.PressedState(0, true));
    AIM_EXPECT_FALSE(e.PressedState(-1, true));
}

// Reset 后重新开始计边沿
AIM_TEST(key_edge_reset) {
    KeyEdge e;
    AIM_EXPECT_TRUE(e.PressedState(0x01, true));
    e.Reset();
    AIM_EXPECT_TRUE(e.PressedState(0x01, true));
}

int main() {
    return aim::test::run_all();
}
