#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "core/spi.h"
#include "core/frame_pipeline.h"
#include "core/config.h"
#include "aim/scope.h"
#include "aim/aim_calc.h"
#include "aim/lead.h"
#include "aim/smoothing.h"
#include "fov/fov_math.h"
#include "opsec/behavior.h"
#include "opsec/pacing.h"
#include "opsec/process_guard.h"

// 数据面编排（§4.3）：
//   Capture → Infer → 类别过滤/NMS/坐标映射 → 目标选择 → 瞄准计算 → Actuator
// 各段通过 SPI 注入，便于用 Fake 实现做集成测试与无硬件干跑。
namespace aim {

struct PipelineSettings {
    float screen_w = 1920.f;
    float screen_h = 1080.f;
    int imgsz = 640;
    float conf_threshold = 0.35f;
    float iou_threshold = 0.45f;
    std::vector<int> class_whitelist = {0};
    // 当前模型声明的类别名（加载时由 RunLoop/preview 回灌，供日志与 UI 使用）
    std::vector<std::string> class_names;
    AimConfig aim;
    ScopeSpec scope;
    Fov fov;
    // FOV 的来源配置（§5.8.2）。填了它、且 fov_reestimatable=true 时，管线会在
    // 「采集器上报的真实屏幕尺寸」与「启动时识别的尺寸」不一致时按真实宽高比重算 FOV。
    // 手工构造 PipelineSettings 的测试不填这两个字段 → 行为完全不变。
    FovConfig fov_cfg;
    bool fov_reestimatable = false;
    TargetSelection selection = TargetSelection::NearestCrosshair;
    Vec2F recoil_comp_px{};
    bool armed = true;       // 热键是否激活（ActivationState::Armed）
    bool bezier = false;     // 下发指令是否走贝塞尔
    int duration_ms = 0;
    spi::CaptureSpec capture_spec;

    // M9 防检测（默认关闭，避免影响既有确定性测试；由配置层显式开启）
    bool behavior_shaping = false;
    bool switch_hysteresis = false;
    uint32_t anti_detect_seed = 0;
    AntiDetectConfig anti_detect;

    // M9 L2 失焦即停（§5.12.2）：game_title 为游戏窗口标题子串（空 = 不判定焦点）。
    // 默认关闭；开启后失焦时不推理、不下发、隐藏覆盖层。
    bool focus_pause = false;
    std::string game_title;
};

struct PipelineDeps {
    spi::ICapture* capture = nullptr;
    spi::IBackend* backend = nullptr;
    spi::IActuator* actuator = nullptr;
    // 可选：覆盖层。为空则不绘制（无头/测试场景）。
    spi::IOverlay* overlay = nullptr;
};

struct PipelineResult {
    bool ok = false;             // 流程是否正常执行（无帧/推理失败为 false）
    bool has_frame = true;       // 本轮是否取到帧；异步采集下超时为 false（非故障）
    size_t detection_count = 0;  // 后处理后候选数
    bool has_target = false;
    Vec2F delta_px{};
    Vec2F delta_angle_deg{};
    uint32_t seq = 0;
    std::vector<Detection> detections; // 后处理后（屏幕坐标），供覆盖层/控制台
};

class Pipeline {
public:
    void Configure(const PipelineDeps& deps, const PipelineSettings& s);
    // 热重载：只替换参数，不重置内部跟踪状态（sticky/lead/迟滞）
    void ApplySettings(const PipelineSettings& s);
    // 每帧量：激活状态与压枪补偿（避免为改一个字段就重置整条管线）
    void SetArmed(bool v) { s_.armed = v; }
    void SetRecoilComp(const Vec2F& v) { s_.recoil_comp_px = v; }
    // 异步帧源（CapturePump）。设为非空后 ProcessFrame 不再直接 grab，
    // 改为从双缓冲取最新帧；传 nullptr 恢复同步 grab。
    void SetFrameSource(FrameDoubleBuffer* src) { frame_src_ = src; }
    // now_ms < 0 时使用单调时钟（测试可传合成时间）。
    PipelineResult ProcessFrame(int64_t now_ms = -1);
    void Reset();

private:
    PipelineDeps deps_{};
    PipelineSettings s_{};
    std::vector<Detection> dets_;
    // 按真实帧尺寸重算 FOV 的缓存（尺寸没变就不重算，避免每帧分配 ads_levels 副本）
    float cached_w_ = -1.f;
    float cached_h_ = -1.f;
    Fov cached_fov_{};
    bool size_mismatch_logged_ = false;
    // Sticky 目标以索引追踪（Detection 无 track_id）。
    // 纯索引不可靠：NMS 后候选顺序会变，同一索引可能指向完全不同的目标。
    // 因此额外记住锁定时的框，用 IoU 做"同一目标"的粗校验，低于阈值就解锁。
    size_t locked_index_ = static_cast<size_t>(-1);
    RectF locked_box_{};
    bool has_locked_box_ = false;
    uint32_t seq_ = 0;
    LeadPredictor lead_{8}; // §7.2 推进外推
    SmoothingFilter smoothing_; // §5.5.4 轨迹平滑
    int64_t last_now_ms_ = -1;  // 用于计算平滑的 dt
    opsec::BehaviorShaper shaper_{0};
    opsec::TargetHysteresis hysteresis_;
    opsec::ProcessGuard guard_;
    bool overlay_hidden_ = false; // 失焦隐藏状态，避免每帧重复 ShowWindow
    FrameDoubleBuffer* frame_src_ = nullptr; // 异步帧源（空 = 同步 grab）
    OwnedFrame owned_;                       // 异步模式下复用的帧缓冲，避免每帧分配
};

} // namespace aim
