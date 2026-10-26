#pragma once

// 分辨率识别（§5.8.1）：枚举显示模式 / 取当前分辨率。
// 平台相关（Win32），仅提供查询；坐标原点/客户区偏移由采集器负责。
//
// 口径约定（重要）：
//   * 这里的返回值是**物理像素**（DEVMODE.dmPelsWidth/Height），不是 DPI 缩放后的逻辑值。
//     调用方若要跟 UI 的 CSS 像素对齐，需自行除以 scaleFactor。
//   * 多显示器下"主屏"与"采集的那块屏"不是一回事：采集走 capture.output_monitor，
//     因此 RunLoop 优先用 GetMonitorResolution(output_monitor)，失败才退回主屏。
//     真正的权威值来自采集器上报的帧尺寸（DXGI 的 DXGI_OUTPUT_DESC），
//     见 Pipeline::ProcessFrame 对 frame.screen_w/h 的使用。
namespace aim {

struct DisplayMode {
    int w = 0;
    int h = 0;
};

// 当前主显示器分辨率（物理像素）；失败返回 false，且不修改 w/h。
bool GetPrimaryResolution(int& w, int& h);

// 指定显示器的当前分辨率（物理像素）。index 与 DXGI 的 EnumOutputs 序号一致
// （0 = 第一块屏，通常对应 \\.\DISPLAY1）。失败返回 false，且不修改 w/h。
bool GetMonitorResolution(int index, int& w, int& h);

// 枚举显示模式（已按宽高去重、按面积升序），最多写入 max_count 个；返回实际数量。
// 注意：同一分辨率会因刷新率/色深不同重复出现，这里已去重，否则下拉框里会有几百项。
int EnumerateDisplayModes(DisplayMode* out, int max_count);

} // namespace aim
