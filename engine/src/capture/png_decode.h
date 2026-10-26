#pragma once
#include <cstdint>
#include <string>
#include <vector>

// PNG 解码（scrcpy 降级路径 `adb exec-out screencap -p` 返回 PNG）。
// 用 GDI+ 内存流解码并缩放到目标尺寸，输出 BGRA8。
namespace aim::capture {

bool DecodePngToBgra(const uint8_t* data, size_t len,
                     int out_w, int out_h,
                     std::vector<uint8_t>& out,
                     std::string* err = nullptr);

} // namespace aim::capture
