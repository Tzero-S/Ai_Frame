#pragma once
#include "core/config.h"
#include "fov/fov_math.h"

// FOV 估计（§5.8.2）：自动初值由宽高比查表，vfov 由 hfov 推出；支持手动覆盖。
//
// 口径（已与文档 §13.2 对齐）：16:9 的 HFOV=103°（查表）；VFOV 由 §5.8.2 公式
// vfov = 2·atan(tan(hfov/2)/aspect) 推出（16:9≈70.5°）。配置中的 59° 仅为存量默认值，
// 自动模式下不参与计算；59° 实际对应 ≈2.22:1 画幅。
namespace aim {

struct FovEstimate {
    Fov fov;               // 最终采用值
    float auto_hfov_deg = 0.f;
    float auto_vfov_deg = 0.f;
    bool manual = false;   // 是否采用手动覆盖
    bool inconsistent = false; // 手动值与自动值偏差 > 5%（UI 黄色提示，不阻断）
    bool ads_applied = false;  // 是否命中开镜倍率表（§5.8.2）
};

FovEstimate EstimateFov(int screen_w, int screen_h, const FovConfig& cfg);

} // namespace aim
