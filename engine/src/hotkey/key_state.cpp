#include "hotkey/key_state.h"

#include <cctype>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace aim {

namespace {

std::string Upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

int VkFromName(const std::string& name) {
    if (name.empty()) return 0;
    const std::string k = Upper(name);

    // 鼠标（§5.9.1：MOUSE1 左键、MOUSE2 右键、MOUSE3 中键、MOUSE4/5 侧键）
    if (k == "MOUSE1") return 0x01; // VK_LBUTTON
    if (k == "MOUSE2") return 0x02; // VK_RBUTTON
    if (k == "MOUSE3") return 0x04; // VK_MBUTTON
    if (k == "MOUSE4") return 0x05; // VK_XBUTTON1
    if (k == "MOUSE5") return 0x06; // VK_XBUTTON2

    if (k == "SPACE") return 0x20;
    if (k == "TAB") return 0x09;
    if (k == "SHIFT") return 0x10;
    if (k == "CTRL" || k == "CONTROL") return 0x11;
    if (k == "ALT") return 0x12;
    if (k == "ENTER" || k == "RETURN") return 0x0D;
    if (k == "ESC" || k == "ESCAPE") return 0x1B;
    if (k == "UP") return 0x26;
    if (k == "DOWN") return 0x28;
    if (k == "LEFT") return 0x25;
    if (k == "RIGHT") return 0x27;

    // F1..F12
    if (k.size() >= 2 && k[0] == 'F') {
        bool digits = true;
        for (std::size_t i = 1; i < k.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(k[i]))) { digits = false; break; }
        }
        if (digits) {
            const int n = std::stoi(k.substr(1));
            if (n >= 1 && n <= 24) return 0x70 + (n - 1); // VK_F1 = 0x70
        }
    }

    // NUMPAD0..NUMPAD9
    if (k.rfind("NUMPAD", 0) == 0 && k.size() == 7 && std::isdigit(static_cast<unsigned char>(k[6]))) {
        return 0x60 + (k[6] - '0'); // VK_NUMPAD0 = 0x60
    }

    // 单字符：A-Z / 0-9
    if (k.size() == 1) {
        const char c = k[0];
        if (c >= 'A' && c <= 'Z') return c;          // VK_A = 0x41
        if (c >= '0' && c <= '9') return c;          // VK_0 = 0x30
    }

    return 0;
}

bool IsKeyDown(int vk) {
    if (vk <= 0) return false;
#ifdef _WIN32
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    return false;
#endif
}

bool KeyEdge::Pressed(int vk) {
    if (vk <= 0) return false;
    return PressedState(vk, IsKeyDown(vk));
}

bool KeyEdge::PressedState(int vk, bool down) {
    if (vk <= 0) return false;
    auto it = prev_.find(vk);
    const bool was = (it != prev_.end()) ? it->second : false;
    prev_[vk] = down;
    return down && !was;
}

void KeyEdge::Reset() { prev_.clear(); }

} // namespace aim
