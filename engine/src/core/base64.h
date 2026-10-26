#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Base64（IPC 传输原始 BGRA 帧用）。
namespace aim::base64 {

std::string Encode(const uint8_t* data, size_t len);
inline std::string Encode(const std::vector<uint8_t>& v) {
    return Encode(v.data(), v.size());
}

bool Decode(const std::string& text, std::vector<uint8_t>& out, std::string* err = nullptr);

} // namespace aim::base64
