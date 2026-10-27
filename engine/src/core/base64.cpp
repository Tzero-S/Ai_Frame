#include "core/base64.h"

namespace aim::base64 {

namespace {
constexpr const char* kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 反查表编译期生成：旧实现是"函数内 static + bool 懒初始化"，
// 多线程首次同时调用会竞争（虽然当前调用路径单线程，但没有理由留这个坑）。
struct RevTable {
    int8_t v[256]{};
    constexpr RevTable() {
        for (int i = 0; i < 256; ++i) v[i] = -1;
        for (int i = 0; i < 64; ++i) {
            v[static_cast<uint8_t>(kAlphabet[i])] = static_cast<int8_t>(i);
        }
    }
};
constexpr RevTable kRev{};
} // namespace

std::string Encode(const uint8_t* data, size_t len) {
    std::string out;
    if (!data && len) return out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        const bool has1 = (i + 1 < len);
        const bool has2 = (i + 2 < len);
        if (has1) v |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (has2) v |= static_cast<uint32_t>(data[i + 2]);

        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(has1 ? kAlphabet[(v >> 6) & 0x3F] : '=');
        out.push_back(has2 ? kAlphabet[v & 0x3F] : '=');
    }
    return out;
}

bool Decode(const std::string& text, std::vector<uint8_t>& out, std::string* err) {
    out.clear();
    const int8_t* rev = kRev.v;

    // 用无符号 32 位累加：有符号 int 在连续左移 6 位时会溢出（UB），
    // 无符号回绕是定义行为，且 (val >> bits) & 0xFF 取低字节始终正确。
    uint32_t val = 0;
    int bits = -8;
    for (char c : text) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        if (c == '=') break;

        const int8_t x = rev[static_cast<uint8_t>(c)];
        if (x < 0) {
            if (err) *err = "invalid base64 character";
            out.clear();
            return false;
        }
        val = (val << 6) | static_cast<uint32_t>(x);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

} // namespace aim::base64
