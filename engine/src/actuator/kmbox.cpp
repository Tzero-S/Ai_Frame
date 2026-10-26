#include "actuator/kmbox.h"

namespace aim {

namespace {
inline void PutU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
inline void PutU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
inline void PutI32(uint8_t* p, int32_t v) { PutU32(p, static_cast<uint32_t>(v)); }
} // namespace

std::array<uint8_t, kKmboxPacketSize>
BuildKmboxPacket(const std::array<uint8_t, 6>& mac,
                 uint32_t rand_val,
                 uint16_t index_pts,
                 KmboxCmd cmd,
                 const KmboxMove& move) {
    std::array<uint8_t, kKmboxPacketSize> pkt{};

    for (int i = 0; i < 6; ++i) pkt[0x00 + i] = mac[i];        // MAC
    PutU32(&pkt[0x06], rand_val);                              // rand
    PutU16(&pkt[0x0A], index_pts);                             // index_pts
    PutU32(&pkt[0x0C], static_cast<uint32_t>(cmd));            // cmd

    // mouse structure @0x10
    pkt[0x10] = move.buttons;
    // 0x11..0x13 reserved（已为 0）
    PutI32(&pkt[0x14], move.dx);
    PutI32(&pkt[0x18], move.dy);
    PutI32(&pkt[0x1C], move.wheel);
    // 其余补 0
    return pkt;
}

bool ParseUid(const std::string& uid, std::array<uint8_t, 4>& out) {
    if (uid.size() != 8) return false;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (int i = 0; i < 4; ++i) {
        const int hi = hex(uid[i * 2]);
        const int lo = hex(uid[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

} // namespace aim
