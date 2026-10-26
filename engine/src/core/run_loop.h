#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "core/config.h"
#include "core/config_store.h"
#include "core/capture_pump.h"
#include "core/pipeline.h"
#include "core/runtime.h"
#include "core/settings_builder.h"
#include "core/spi.h"
#include "hotkey/hotkey_manager.h"
#include "hotkey/key_state.h"
#include "weapon/pattern_store.h"
#include "weapon/recoil_engine.h"
#include "weapon/trigger_engine.h"

// 生产运行循环（§4.3 数据面）：
//   热键 → 压枪补偿 → Pipeline（采集/推理/后处理/瞄准）→ 扳机 → 状态快照
// 与 --preview 的区别：armed 由热键驱动、执行端为真实通道、可挂 IPC 热重载。
namespace aim {

struct RunStats {
    int frames = 0;
    int failures = 0;
    double pre_ms_sum = 0.0;
    double infer_ms_sum = 0.0;
    double post_ms_sum = 0.0;
    int64_t elapsed_ms = 0;
};

class RunLoop {
public:
    RunLoop();
    ~RunLoop(); // 显式停泵并断开帧源，保证线程先于 capture 退出

    RunLoop(const RunLoop&) = delete;
    RunLoop& operator=(const RunLoop&) = delete;

    // 拥有型初始化：按配置从 Registry 创建设备；dry_run 时执行端替换为 null 通道。
    // frames_limit == 0 表示无限循环。
    Result<void> Init(const Config& cfg, const std::string& model_path,
                      int frames_limit, bool with_overlay, bool dry_run);

    // 注入型初始化：外部提供设备（测试/干跑），不接管所有权。
    // async=false 走同步 grab（既有测试依赖确定性时序）；true 时用采集泵 + 双缓冲。
    void InitWithDeps(const Config& cfg, const BuiltSettings& built,
                      spi::ICapture* cap, spi::IBackend* be,
                      spi::IActuator* act, spi::IOverlay* ov, int frames_limit,
                      bool async = false);

    // 挂接配置中心：版本变化时热重载（设备相关字段变更需重启，会在状态中标记）。
    void AttachConfigStore(ConfigStore* store) { store_ = store; }

    int Run();                              // 返回进程退出码
    void RequestStop() { stop_.store(true); }
    const RunStats& stats() const { return stats_; }
    const EngineRuntime& runtime() const { return runtime_; }
    const BuiltSettings& settings() const { return built_; }
    bool restart_required() const { return restart_required_; }
    const std::string& restart_reason() const { return restart_reason_; }

    // 每帧回调（推 IPC / 打印），在帧处理后调用。
    std::function<void()> on_frame;

    // 键状态探针（默认读真实键盘；单测/回放可注入）
    void SetKeyProbe(std::function<bool(int)> probe) { key_probe_ = std::move(probe); }

private:
    void UpdateHotkeys(int64_t now_ms);
    void UpdateRecoil(int64_t now_ms);
    void UpdateTrigger(int64_t now_ms, const PipelineResult& r);
    void ReapplySettings();
    void ConfigureWeaponAndHotkeys();
    void LoadRecoilPatterns();
    void ConfigurePacer();
    bool KeyDown(int vk) const { return key_probe_ ? key_probe_(vk) : IsKeyDown(vk); }

    Config cfg_{};
    BuiltSettings built_{};
    Pipeline pipeline_;
    EngineRuntime runtime_;
    ConfigStore* store_ = nullptr;
    int applied_version_ = -1;
    bool settings_dirty_ = false;
    bool restart_required_ = false;
    std::string restart_reason_;

    // 每帧量（不写回 settings，避免为改一个字段重置管线状态）
    bool armed_ = false;
    Vec2F recoil_px_{};

    // 热键 / 武器（§5.5.3 / §5.7）
    ActivationState activation_;
    KeyEdge edges_;
    RecoilEngine recoil_;
    PatternStore patterns_;
    opsec::IdlePacer pacer_;
    TriggerEngine trigger_;
    float alpha_ = 0.35f;                 // 吸附强度（F11/F12 调整）
    TargetPreset preset_ = TargetPreset::Head;
    bool hud_visible_ = true;
    int64_t last_recoil_ms_ = 0;
    int64_t last_trigger_ms_ = 0;
    std::function<bool(int)> key_probe_;

    // 拥有型设备（Init 路径）
    std::unique_ptr<spi::ICapture> own_capture_;
    // 必须声明在 own_capture_ 之后：成员按逆序析构，保证采集泵先停、capture 后销毁
    CapturePump pump_;
    // 最近一次成功取到帧的时刻（ms）；用于"采集长时间无帧"的看门狗
    int64_t last_frame_ms_ = -1;
    float recoil_strength_ = -1.f; // 当前生效的压枪强度（按倍率切换时用）
    std::unique_ptr<spi::IBackend> own_backend_;
    std::unique_ptr<spi::IActuator> own_actuator_;
    std::unique_ptr<spi::IOverlay> own_overlay_;

    // 实际使用（拥有型或注入型）
    spi::ICapture* capture_ = nullptr;
    spi::IBackend* backend_ = nullptr;
    spi::IActuator* actuator_ = nullptr;
    spi::IOverlay* overlay_ = nullptr;

    int frames_limit_ = 0;
    std::atomic<bool> stop_{false};
    RunStats stats_{};
};

} // namespace aim
