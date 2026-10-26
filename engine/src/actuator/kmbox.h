#pragma once
#include <array>
#include <cstdint>
#include <string>

// KMBox UDP 报文（§8.3，逆向所得，待硬件手册复核）。
//   偏移 长度 字段
//   0x00 6    MAC
//   0x06 4    rand
//   0x0A 2    index_pts
//   0x0C 4    cmd
//   0x10 56   mouse structure（按键位/移动量/滚轮）
//   总长 72
namespace aim {

enum class KmboxCmd : uint32_t {
    Connect = 0xAF3C2828,
    Move    = 0xAEDE7345,
    Bezier  = 0xA238455A,
    Monitor = 0x27388020,
    Mask    = 0x23234343,
};

constexpr size_t kKmboxPacketSize = 72;

struct KmboxMove {
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t wheel = 0;
    uint8_t buttons = 0;
};

// 构造 72 字节报文（小端）。mouse 结构采用保守布局：
//   0x10 buttons[1] | 0x11 reserved[3] | 0x14 dx(i32) | 0x18 dy(i32) | 0x1C wheel(i32) | 其余补 0
std::array<uint8_t, kKmboxPacketSize>
BuildKmboxPacket(const std::array<uint8_t, 6>& mac,
                 uint32_t rand_val,
                 uint16_t index_pts,
                 KmboxCmd cmd,
                 const KmboxMove& move);

// UID 字符串（8 位十六进制）→ 4 字节；非法返回 false。
bool ParseUid(const std::string& uid, std::array<uint8_t, 4>& out);

} // namespace aim
