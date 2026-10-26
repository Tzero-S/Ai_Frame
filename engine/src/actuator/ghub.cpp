#include "actuator/ghub.h"
#include <algorithm>

namespace aim {

std::vector<GhubReport> GhubSplit(int dx, int dy, int max_step) {
    std::vector<GhubReport> out;
    if (max_step <= 0) max_step = 127;
    if (dx == 0 && dy == 0) return out;

    int rx = dx;
    int ry = dy;
    // 安全上限，避免异常输入导致无限循环
    const int kMaxReports = 1 << 16;
    while ((rx != 0 || ry != 0) && static_cast<int>(out.size()) < kMaxReports) {
        const int sx = std::clamp(rx, -max_step, max_step);
        const int sy = std::clamp(ry, -max_step, max_step);
        GhubReport r;
        r.report_id = 0;
        r.dx = static_cast<int8_t>(sx);
        r.dy = static_cast<int8_t>(sy);
        r.reserved = 0;
        out.push_back(r);
        rx -= sx;
        ry -= sy;
    }
    return out;
}

} // namespace aim
