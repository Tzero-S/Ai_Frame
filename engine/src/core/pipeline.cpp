#include "core/pipeline.h"
#include "core/clock.h"
#include "core/logger.h"
#include "fov/fov_estimator.h"
#include "postprocess/class_filter.h"
#include "postprocess/nms.h"
#include "postprocess/yolo_decoder.h"
#include "aim/target_select.h"
#include <algorithm>
#include <cmath>

namespace aim {

void Pipeline::Configure(const PipelineDeps& deps, const PipelineSettings& s) {
    deps_ = deps;
    s_ = s;
    dets_.clear();
    locked_index_ = static_cast<size_t>(-1);
    seq_ = 0;
    lead_.Reset();
    // 尺寸/FOV 缓存跟随新设置失效（换屏、改分辨率后要重新判定）
    cached_w_ = -1.f;
    cached_h_ = -1.f;
    size_mismatch_logged_ = false;
    shaper_ = opsec::BehaviorShaper(s_.anti_detect_seed);
    shaper_.Configure(s_.anti_detect);
    hysteresis_.Configure(s_.anti_detect.switch_hysteresis_frames);
    guard_.Configure(s_.focus_pause, s_.game_title);
}

void Pipeline::ApplySettings(const PipelineSettings& s) {
    s_ = s;
    // 抖动/迟滞/焦点守卫需要跟随配置，但不触碰 dets_/locked_index_/lead_
    shaper_.Configure(s_.anti_detect);
    hysteresis_.Configure(s_.anti_detect.switch_hysteresis_frames);
    guard_.Configure(s_.focus_pause, s_.game_title);
    // FOV 重算缓存必须失效：用户改了 manual_override/ads 后，缓存里还是旧值，
    // 只有帧尺寸变化才会刷新 —— 那就会一直用旧的 FOV。
    cached_w_ = -1.f;
    cached_h_ = -1.f;
    size_mismatch_logged_ = false;
}

void Pipeline::Reset() {
    dets_.clear();
    locked_index_ = static_cast<size_t>(-1);
    seq_ = 0;
    lead_.Reset();
    hysteresis_.Reset();
    smoothing_.Reset();
    last_now_ms_ = -1;
    overlay_hidden_ = false;
    cached_w_ = -1.f;
    cached_h_ = -1.f;
    size_mismatch_logged_ = false;
}

PipelineResult Pipeline::ProcessFrame(int64_t now_ms) {
    PipelineResult r;
    if (!deps_.capture || !deps_.backend || !deps_.actuator) return r;

    const int64_t now = (now_ms >= 0) ? now_ms : Clock::NowMs();

    // 1) 采集：同步 grab 或从异步双缓冲取最新帧
    spi::Frame frame;
    if (frame_src_) {
        if (!frame_src_->Acquire(owned_)) {
            // 本轮没有新帧（采集慢于推理）。这不是故障，调用方跳过即可。
            r.ok = true;
            r.has_frame = false;
            return r;
        }
        frame.bgra = owned_.bgra.data();
        frame.w = owned_.w;
        frame.h = owned_.h;
        frame.screen_w = owned_.screen_w;
        frame.screen_h = owned_.screen_h;
        frame.src_x = owned_.src_x;
        frame.src_y = owned_.src_y;
        frame.src_w = owned_.src_w;
        frame.src_h = owned_.src_h;
        frame.seq = owned_.seq;
        frame.ts = owned_.ts_us;
        frame.valid = true;
    } else {
        frame = deps_.capture->grab(s_.capture_spec);
        if (!frame.valid) return r;
    }

    // 分辨率的权威值 = 采集器上报的屏幕尺寸（DXGI 读 DXGI_OUTPUT_DESC，GDI 读虚拟屏幕）。
    // 启动时识别一次的值可能不准：多显示器（配的屏不是主屏）、DPI 缩放、
    // 运行中改了分辨率或全屏独占切了模式。帧自带尺寸，用它兜住这些情况。
    const float screen_w = (frame.screen_w > 0) ? static_cast<float>(frame.screen_w) : s_.screen_w;
    const float screen_h = (frame.screen_h > 0) ? static_cast<float>(frame.screen_h) : s_.screen_h;
    Fov fov = s_.fov;
    const bool size_mismatch = frame.screen_w > 0 && frame.screen_h > 0 &&
                               (std::fabs(screen_w - s_.screen_w) > 0.5f ||
                                std::fabs(screen_h - s_.screen_h) > 0.5f);
    if (size_mismatch && s_.fov_reestimatable) {
        if (std::fabs(screen_w - cached_w_) > 0.5f || std::fabs(screen_h - cached_h_) > 0.5f) {
            cached_w_ = screen_w;
            cached_h_ = screen_h;
            cached_fov_ = EstimateFov(static_cast<int>(screen_w), static_cast<int>(screen_h),
                                      s_.fov_cfg)
                              .fov;
        }
        fov = cached_fov_;
        if (!size_mismatch_logged_) {
            size_mismatch_logged_ = true;
            AIM_ENGINE(Warn, "采集尺寸 " + std::to_string(static_cast<int>(screen_w)) + "x" +
                                 std::to_string(static_cast<int>(screen_h)) + " 与识别值 " +
                                 std::to_string(static_cast<int>(s_.screen_w)) + "x" +
                                 std::to_string(static_cast<int>(s_.screen_h)) +
                                 " 不一致，已按实际尺寸重算 FOV");
        }
    }

    // M9 L2 失焦即停（§5.12.2）：默认关闭。失焦时不推理、不下发、隐藏覆盖层。
    if (s_.focus_pause && guard_.ShouldPause()) {
        if (deps_.overlay && !overlay_hidden_) {
            deps_.overlay->set_visible(false);
            overlay_hidden_ = true;
        }
        // 清空跟踪状态：失焦期间的陈旧样本不应参与恢复后的外推
        locked_index_ = static_cast<size_t>(-1);
        lead_.Reset();
        r.ok = true; // 正常空闲帧（非故障），避免调用方误判采集/推理异常
        return r;
    }
    if (deps_.overlay && overlay_hidden_) {
        deps_.overlay->set_visible(true);
        overlay_hidden_ = false;
    }

    // 2) 推理（返回模型输入空间的解码结果）
    dets_.clear();
    if (!deps_.backend->infer(frame, dets_)) return r;

    // 3) 后处理：置信度 → 类别白名单 → NMS → 屏幕坐标
    dets_.erase(std::remove_if(dets_.begin(), dets_.end(),
                               [&](const Detection& d) { return d.score < s_.conf_threshold; }),
                dets_.end());
    FilterClasses(dets_, s_.class_whitelist);
    Nms(dets_, s_.iou_threshold);
    // ROI 感知的坐标映射（§5.2.2 / §5.4）：
    //   采集器给出的是屏幕中心 edge×edge 裁剪，模型输入是该 ROI 的重采样；
    //   src_w/src_h 未提供时退化为"帧即整屏"，保持与旧实现/测试假件兼容。
    const float roi_x = static_cast<float>(frame.src_x);
    const float roi_y = static_cast<float>(frame.src_y);
    const float roi_w = (frame.src_w > 0) ? static_cast<float>(frame.src_w) : screen_w;
    const float roi_h = (frame.src_h > 0) ? static_cast<float>(frame.src_h) : screen_h;
    ScaleRoiToScreen(dets_, static_cast<float>(s_.imgsz), static_cast<float>(s_.imgsz),
                     roi_x, roi_y, roi_w, roi_h);
    r.detection_count = dets_.size();
    r.detections = dets_; // 覆盖层/控制台预览用（屏幕坐标）

    // 覆盖层绘制（可选）：detections 已是屏幕坐标，覆盖层按屏幕尺寸铺满
    if (deps_.overlay) deps_.overlay->draw(frame, r.detections);

    if (!s_.armed) {
        locked_index_ = static_cast<size_t>(-1);
        lead_.Reset();
        r.ok = true;
        return r;
    }

    // 4) 目标选择
    const Detection* locked = (locked_index_ < dets_.size()) ? &dets_[locked_index_] : nullptr;
    // 准星基准点（§5.5.1）：多数游戏是屏幕几何中心，个别游戏需要偏移
    const Vec2F center{ screen_w * 0.5f + s_.aim.crosshair_offset_x,
                        screen_h * 0.5f + s_.aim.crosshair_offset_y };
    const Detection* sel = SelectTarget(dets_, s_.selection, s_.scope,
                                        screen_w, screen_h, fov, locked, &center);
    if (!sel) {
        locked_index_ = static_cast<size_t>(-1);
        lead_.Reset();
        smoothing_.Reset();
        r.ok = true;
        return r;
    }
    size_t sel_idx = static_cast<size_t>(sel - dets_.data());

    // M9 目标切换迟滞：新目标需连续 N 帧才切换（§5.12.1）
    if (s_.switch_hysteresis && s_.anti_detect.level != AntiDetectLevel::Off &&
        locked_index_ < dets_.size() && sel_idx != locked_index_) {
        if (!hysteresis_.AllowSwitch(static_cast<int>(sel_idx),
                                     static_cast<int>(locked_index_))) {
            sel = &dets_[locked_index_];
            sel_idx = locked_index_;
        }
    } else {
        hysteresis_.Reset();
    }
    locked_index_ = sel_idx;

    // 5) 瞄准计算（几何 + 弹道补偿 + 推进外推）
    const PointF anchor = ComputeTargetPoint(
        *sel, s_.aim.target_point.preset, s_.aim.target_point.offset_y_ratio, s_.aim.micro_adjust);
    lead_.AddSample(static_cast<double>(now), Vec2F{ anchor.x, anchor.y });

    Vec2F comp = s_.recoil_comp_px;
    if (s_.aim.smoothing.lead > 0.f) {
        const Vec2F pred = lead_.Predict(
            static_cast<double>(s_.aim.latency_compensation_ms), s_.aim.smoothing.lead);
        comp.x += pred.x - anchor.x;
        comp.y += pred.y - anchor.y;
    }

    const AimResult aim = ComputeAim(*sel, s_.aim, screen_w, screen_h, fov, comp);

    // 5.5) 轨迹平滑（§5.5.4）：把逐帧位移变成连续轨迹。
    // 首帧直通，因此单帧场景（预览/测试）输出与未平滑时一致。
    Vec2F smoothed = aim.delta_px;
    if (s_.aim.smoothing.enabled) {
        smoothing_.model = (s_.aim.smoothing.model == "ema") ? SmoothingModel::Ema
                                                             : SmoothingModel::Critical;
        smoothing_.alpha = s_.aim.smoothing.alpha;
        smoothing_.omega = s_.aim.smoothing.omega;
        smoothing_.max_jerk = s_.aim.smoothing.max_jerk;
        const float dt_s = (last_now_ms_ >= 0)
                               ? static_cast<float>(now - last_now_ms_) * 0.001f
                               : (1.f / 120.f);
        smoothed = smoothing_.Apply(aim.delta_px, dt_s);
    }
    last_now_ms_ = now;

    // 6) 执行（M9 行为层：抖动 + 时机随机化）
    Vec2F out_delta = smoothed;
    int out_duration = s_.duration_ms;
    if (s_.behavior_shaping) {
        const auto shaped = shaper_.ShapeMove(out_delta, out_duration);
        out_delta = shaped.delta;
        out_duration = shaped.duration_ms;
    }

    spi::MoveCmd mv;
    mv.dx_px = out_delta.x;
    mv.dy_px = out_delta.y;
    mv.duration_ms = out_duration;
    mv.bezier = s_.bezier;
    mv.seq = ++seq_;
    deps_.actuator->move(mv);

    r.ok = true;
    r.has_target = true;
    r.delta_px = aim.delta_px;
    r.delta_angle_deg = aim.delta_angle_deg;
    r.seq = mv.seq;
    return r;
}

} // namespace aim
