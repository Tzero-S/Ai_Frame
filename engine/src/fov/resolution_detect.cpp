#include "fov/resolution_detect.h"

#include <algorithm>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace aim {

#ifdef _WIN32

namespace {

// 把 DXGI 输出序号映射成 GDI 设备名。序号 0 → "\\.\DISPLAY1"。
std::string DisplayDeviceName(int index) {
    return "\\\\.\\DISPLAY" + std::to_string(index + 1);
}

bool QueryDevice(const char* device, int& w, int& h) {
    DEVMODEA dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsA(device, ENUM_CURRENT_SETTINGS, &dm)) return false;
    const int qw = static_cast<int>(dm.dmPelsWidth);
    const int qh = static_cast<int>(dm.dmPelsHeight);
    if (qw <= 0 || qh <= 0) return false;
    w = qw;
    h = qh;
    return true;
}

} // namespace

bool GetPrimaryResolution(int& w, int& h) {
    // NULL = 当前显示设备（桌面所在的那块，即主屏）。
    // 失败时**不修改** w/h，由调用方决定退化值并打日志——旧实现直接返回 false
    // 但调用方忽略返回值，于是静默用了 1920x1080，问题极难发现。
    return QueryDevice(nullptr, w, h);
}

bool GetMonitorResolution(int index, int& w, int& h) {
    if (index < 0) return false;
    // 不做内部兜底：序号越界（只有一块屏却配了 output_monitor=1）必须让调用方知道，
    // 由它决定退回主屏并告警。悄悄退回会让"配置写错了"表现成"分辨率识别不准"。
    return QueryDevice(DisplayDeviceName(index).c_str(), w, h);
}

int EnumerateDisplayModes(DisplayMode* out, int max_count) {
    if (!out || max_count <= 0) return 0;

    std::vector<DisplayMode> all;
    DWORD idx = 0;
    DEVMODEA dm{};
    dm.dmSize = sizeof(dm);
    while (EnumDisplaySettingsA(nullptr, idx, &dm)) {
        const int w = static_cast<int>(dm.dmPelsWidth);
        const int h = static_cast<int>(dm.dmPelsHeight);
        if (w > 0 && h > 0) {
            const bool dup = std::any_of(all.begin(), all.end(), [w, h](const DisplayMode& m) {
                return m.w == w && m.h == h;
            });
            if (!dup) all.push_back(DisplayMode{w, h});
        }
        ++idx;
        dm.dmSize = sizeof(dm);
    }

    // 按面积升序，同面积按宽度：下拉框顺序稳定，便于用户按"从小到大"找
    std::sort(all.begin(), all.end(), [](const DisplayMode& a, const DisplayMode& b) {
        const long long sa = static_cast<long long>(a.w) * a.h;
        const long long sb = static_cast<long long>(b.w) * b.h;
        if (sa != sb) return sa < sb;
        return a.w < b.w;
    });

    const int n = std::min<int>(static_cast<int>(all.size()), max_count);
    for (int i = 0; i < n; ++i) out[i] = all[static_cast<size_t>(i)];
    return n;
}

#else

// 非 Windows：**不伪造**分辨率。旧实现返回 true + 1920x1080，会让调用方以为识别成功，
// 在 Linux/macOS 上静默用错误尺寸算 FOV 与准星中心。这里如实失败，由调用方退化并告警。
bool GetPrimaryResolution(int&, int&) { return false; }

bool GetMonitorResolution(int, int&, int&) { return false; }

int EnumerateDisplayModes(DisplayMode*, int) { return 0; }

#endif

} // namespace aim
