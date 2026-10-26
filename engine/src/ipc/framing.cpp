#include "ipc/framing.h"
#include <cstring>

namespace aim::ipc {

std::string EncodeFrame(const std::string& payload) {
    const uint32_t n = static_cast<uint32_t>(payload.size());
    std::string out;
    out.resize(4 + payload.size());
    out[0] = static_cast<char>(n & 0xFF);
    out[1] = static_cast<char>((n >> 8) & 0xFF);
    out[2] = static_cast<char>((n >> 16) & 0xFF);
    out[3] = static_cast<char>((n >> 24) & 0xFF);
    if (!payload.empty()) std::memcpy(&out[4], payload.data(), payload.size());
    return out;
}

bool TryDecodeFrame(std::string& buffer, std::string& out_payload) {
    if (buffer.size() < 4) return false;

    const uint32_t n =
        static_cast<uint32_t>(static_cast<uint8_t>(buffer[0])) |
        (static_cast<uint32_t>(static_cast<uint8_t>(buffer[1])) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(buffer[2])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(buffer[3])) << 24);

    if (n > kMaxFrameBytes) {
        buffer.clear();
        return false;
    }
    if (buffer.size() < 4u + n) return false;

    out_payload.assign(buffer.data() + 4, n);
    buffer.erase(0, 4u + n);
    return true;
}

} // namespace aim::ipc
