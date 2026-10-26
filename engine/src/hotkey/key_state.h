#pragma once
#include <map>
#include <string>

// 键名 ⇄ 虚拟键码 与 物理键状态读取（§5.9）。
// 键名口径与配置一致：MOUSE1..MOUSE5 / F1..F12 / NUMPAD0..9 / UP|DOWN|LEFT|RIGHT /
// A..Z / 0..9 / SPACE|TAB|SHIFT|CTRL|ALT|ENTER|ESC。
namespace aim {

// 未知键名返回 0（调用方据此判定"未绑定"）。
int VkFromName(const std::string& name);

// 物理键是否按下（Windows: GetAsyncKeyState；其他平台恒 false）。
bool IsKeyDown(int vk);

// 边沿检测：Pressed() 在"由抬起变为按下"的那一帧返回 true。
class KeyEdge {
public:
    bool Pressed(int vk);
    // 注入型边沿：直接给出当前按下状态（单测/回放用，不读真实键盘）
    bool PressedState(int vk, bool down);
    void Reset();

private:
    std::map<int, bool> prev_;
};

} // namespace aim
