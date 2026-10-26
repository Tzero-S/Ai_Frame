#pragma once
#include <cstdint>
#include <vector>

// GHUB 虚拟 HID 执行（§5.6.2，依据 [K] BeeEdit gh.dll 逆向）。
//   报告格式：4 字节 [report_id=0, dx(int8), dy(int8), 0]
//   分片：|dx| 或 |dy| > max_step 时按 ±max_step 步进切分，剩余量延至下一 tick
namespace aim {

struct GhubReport {
    int8_t report_id = 0;
    int8_t dx = 0;
    int8_t dy = 0;
    int8_t reserved = 0;
};

// 将一次位移切分为若干 4 字节报告（逐轴独立限幅，保持总位移不变）。
std::vector<GhubReport> GhubSplit(int dx, int dy, int max_step = 127);

} // namespace aim
