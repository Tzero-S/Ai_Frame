#include "core/settings_builder.h"

#include <algorithm>

#include "fov/fov_estimator.h"
#include "postprocess/class_filter.h"

namespace aim {

BuiltSettings BuildSettings(const Config& cfg, int screen_w, int screen_h) {
    BuiltSettings out;

    const float sw = static_cast<float>(screen_w > 0 ? screen_w : 1920);
    const float sh = static_cast<float>(screen_h > 0 ? screen_h : 1080);

    // ---- 采集（§5.2.2）----
    out.capture_name = to_string(cfg.capture.mode);
    out.capture_spec.output_monitor = cfg.capture.output_monitor;
    const int edge = std::max(1, cfg.capture.roi.edge);
    out.capture_spec.full_frame = (cfg.capture.roi.mode == RoiMode::Full);
    if (cfg.capture.roi.mode == RoiMode::Full) {
        // full：整屏采集，ROI 即屏幕（采集器同时按 full_frame 兜底，见 spi::CaptureSpec）
        out.capture_spec.roi_w = static_cast<int>(sw);
        out.capture_spec.roi_h = static_cast<int>(sh);
    } else if (cfg.capture.mode == CaptureMode::Scrcpy) {
        // 投屏：截边边长来自 capture.scrcpy.edge（§5.2.3）
        const int e = (cfg.capture.scrcpy.edge > 0) ? cfg.capture.scrcpy.edge : edge;
        out.capture_spec.roi_w = e;
        out.capture_spec.roi_h = e;
    } else {
        // center_square：屏幕中心 edge×edge 裁剪
        out.capture_spec.roi_w = edge;
        out.capture_spec.roi_h = edge;
    }

    // ---- 推理（§5.3.1）----
    out.backend_name = to_string(cfg.inference.backend);

    // ---- 执行（§5.6）----
    out.actuator_name = to_string(cfg.actuator.channel);

    // ---- 管线参数 ----
    PipelineSettings& p = out.pipeline;
    p.screen_w = sw;
    p.screen_h = sh;
    p.imgsz = std::max(32, cfg.inference.imgsz);
    p.conf_threshold = cfg.inference.conf_threshold;
    p.iou_threshold = cfg.inference.iou_threshold;
    p.class_whitelist = ResolveClassWhitelist(cfg.inference.selected_class_names,
                                              cfg.inference.class_names).indices;
    p.class_names = cfg.inference.class_names;
    p.aim = cfg.aim;
    p.selection = cfg.aim.selection;

    // 瞄准范围（§5.5.1）：配置字段名与 ScopeSpec 不完全一致，逐项映射
    p.scope.mode = cfg.aim.scope.mode;
    p.scope.half_w = static_cast<float>(cfg.aim.scope.half_w);
    p.scope.half_h = static_cast<float>(cfg.aim.scope.half_h);
    p.scope.radius_px = cfg.aim.scope.radius_px;
    p.scope.hfov_half_deg = cfg.aim.scope.hfov_half;
    p.scope.vfov_half_deg = cfg.aim.scope.vfov_half;

    // FOV（§5.8.2）：自动值按分辨率查表，手动覆盖优先。
    // 同时把来源配置带上：管线在「采集器实际尺寸 ≠ 识别尺寸」时按真实宽高比重算，
    // 避免多显示器/DPI/运行中改分辨率把 FOV 算歪（详见 Pipeline::ProcessFrame）。
    p.fov_cfg = cfg.fov;
    p.fov_reestimatable = true;
    p.fov = EstimateFov(static_cast<int>(sw), static_cast<int>(sh), cfg.fov).fov;

    // 执行细节：KM 贝塞尔开关来自 actuator.kmbox.bezier（§5.6.3）
    p.bezier = cfg.actuator.kmbox.bezier.enabled;
    p.duration_ms = 0;
    p.capture_spec = out.capture_spec;
    p.armed = false; // 由热键状态机驱动（§5.5.3）

    // ---- M9（§5.12）----
    p.anti_detect = cfg.anti_detect;
    p.behavior_shaping = (cfg.anti_detect.level != AntiDetectLevel::Off);
    p.switch_hysteresis = (cfg.anti_detect.level != AntiDetectLevel::Off &&
                           cfg.anti_detect.switch_hysteresis_frames > 0);
    p.anti_detect_seed = 0; // 0 = 随机种子
    // 失焦即停由独立开关控制，不随 level 关闭（它同时是可用性特性）
    p.focus_pause = cfg.anti_detect.hide_on_focus_loss;
    p.game_title = cfg.capture.game_window.title_pattern;
    out.game_title = p.game_title;

    out.target_fps = std::max(1, cfg.inference.target_fps);
    out.zero_tensors = cfg.anti_detect.zero_tensors;
    out.neutral_thread_names = cfg.anti_detect.neutral_thread_names;

    return out;
}

} // namespace aim
