#pragma once
#include <cstdint>
#include <string>

// IPC 帧格式：4 字节小端长度 + UTF-8 JSON 负载（§8.1 管道承载）。
// 长度前缀解决粘包/半包；单帧上限 16MB。
namespace aim::ipc {

constexpr uint32_t kMaxFrameBytes = 16u * 1024u * 1024u;

std::string EncodeFrame(const std::string& payload);

// 从 buffer 头部尝试解出一帧：成功则 out_payload 赋值并从 buffer 移除该帧。
// 数据不足返回 false（buffer 不变）；长度非法时清空 buffer 并返回 false。
bool TryDecodeFrame(std::string& buffer, std::string& out_payload);

} // namespace aim::ipc
