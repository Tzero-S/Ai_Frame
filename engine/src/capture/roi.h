#pragma once
#include "core/config.h"

// ROI 计算（§5.8.1）：中心正方形 / 全屏；窗口化游戏需叠加客户区原点，
// 否则会整体偏移 —— 文档称其为"实践中最高频的 Bug 来源"，故显式建模。
namespace aim {

struct RoiRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// screen_w/h：目标显示区域尺寸；client_origin_x/y：客户区左上角在屏幕的偏移。
RoiRect ComputeRoi(RoiMode mode, int edge, int screen_w, int screen_h,
                   int client_origin_x = 0, int client_origin_y = 0);

} // namespace aim
