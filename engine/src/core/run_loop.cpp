#include "core/run_loop.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

#include "core/clock.h"
#include "core/paths.h"
#include "core/logger.h"
#include "core/registry.h"
#include "fov/resolution_detect.h"
#include "inference/model_validator.h"
#include "postprocess/class_filter.h"
#include "opsec/process_guard.h"
#include "opsec/redact.h"

namespace aim {

namespace {

// 加载弹道库（§5.7.1）。失败不致命：RecoilEngine 会退化为恒量垂直补偿。
void LoadPatternsInto(const Config& cfg, PatternStore& store) {
    store.Clear();
    const std::string& want = cfg.weapon.recoil.patterns_path;
    if (want.empty()) {
        AIM_ENGINE(Warn, "压枪：patterns_path 为空，退化为恒量垂直补偿");
        return;
    }
    const std::string path = ResolveDataFile(want);
    if (path.empty()) {
        AIM_ENGINE(Warn, "压枪：弹道库未找到（" + want + "），退化为恒量垂直补偿");
        return;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        AIM_ENGINE(Warn, "压枪：弹道库打不开（" + path + "），退化为恒量垂直补偿");
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    std::string err;
    if (!store.LoadFromJson(text, &err)) {
        AIM_ENGINE(Warn, "压枪：弹道库解析失败（" + path + "）：" + err);
        return;
    }
    if (store.Count() == 0) {
        AIM_ENGINE(Warn, "压枪：弹道库为空（" + path + "）");
        return;
    }
    const std::string& id = cfg.weapon.recoil.pattern_id;
    if (!store.Has(id)) {
        AIM_ENGINE(Warn, "压枪：弹道库无 pattern_id=" + id + "，退化为恒量垂直补偿");
        return;
    }
    AIM_ENGINE(Info, "压枪：弹道库 " + std::to_string(store.Count()) + " 条，当前 " + id +
                         "（" + path + "）");
}

// 解析屏幕尺寸（物理像素）。顺序：采集目标屏 → 主屏 → 1920×1080 兜底。
// 旧实现直接 GetPrimaryResolution(sw, sh) 且**忽略返回值**，两个问题：
//   ① 采集走 capture.output_monitor，多显示器下拿的是另一块屏的尺寸，
//      准星中心（screen/2）与瞄准半径就系统性偏掉半屏差值；
//   ② 识别失败时静默沿用 1920×1080，日志里完全看不出来。
void ResolveScreenSize(const Config& cfg, int& w, int& h) {
    const int mon = cfg.capture.output_monitor;
    if (GetMonitorResolution(mon, w, h)) {
        AIM_ENGINE(Info, "分辨率识别：显示器 #" + std::to_string(mon) + " " + std::to_string(w) +
                             "x" + std::to_string(h));
        return;
    }
    if (GetPrimaryResolution(w, h)) {
        AIM_ENGINE(Warn, "分辨率识别：显示器 #" + std::to_string(mon) + " 查询失败，退回主屏 " +
                             std::to_string(w) + "x" + std::to_string(h) +
                             "（capture.output_monitor 可能越界）");
        return;
    }
    w = 1920;
    h = 1080;
    AIM_ENGINE(Warn, "分辨率识别失败，退化 1920x1080：FOV 与准星中心可能不准，"
                     "请在控制台「视场 FOV」里手动覆盖");
}

// Run() 的清理守卫：正常返回与所有提前 return（采集超时/连续失败）都必须
// 停泵、断开帧源、停运行时。旧实现只在正常路径做，提前返回时运行时会话残留、
// stats 不完整，而且 stop_ 不复位导致同一对象第二次 Run() 立刻退出。
struct RunCleanup {
    CapturePump& pump;
    Pipeline& pipe;
    EngineRuntime& rt;
    ~RunCleanup() {
        pump.Stop();                  // join 采集线程，确保没人再用 capture
        pipe.SetFrameSource(nullptr);
        rt.Stop();
    }
};

// 帧失败上限：有限帧模式下避免采集/推理持续失败时忙等（与 --preview 口径一致）
int MaxFailures(int frames_limit) {
    return (frames_limit > 0) ? (frames_limit * 10 + 100) : 0;
}

// 设备相关字段（变更需重启才能生效）
bool DeviceIdentityChanged(const BuiltSettings& a, const BuiltSettings& b) {
    return a.capture_name != b.capture_name ||
           a.backend_name != b.backend_name ||
           a.actuator_name != b.actuator_name ||
           a.capture_spec.output_monitor != b.capture_spec.output_monitor ||
           a.capture_spec.roi_w != b.capture_spec.roi_w ||
           a.capture_spec.roi_h != b.capture_spec.roi_h ||
           a.pipeline.imgsz != b.pipeline.imgsz;
}

} // namespace

RunLoop::RunLoop() = default;
RunLoop::~RunLoop() {
    // 关停顺序硬约束：先停采集泵（join 线程）→ 断开帧源 → 之后成员析构才会销毁 capture。
    // 顺序反了就是 use-after-free：采集线程可能正在调用已析构的 ICapture::grab。
    pump_.Stop();
    pipeline_.SetFrameSource(nullptr);
}

Result<void> RunLoop::Init(const Config& cfg, const std::string& model_path,
                           int frames_limit, bool with_overlay, bool dry_run) {
    cfg_ = cfg;
    frames_limit_ = frames_limit;
    // 日志路径脱敏按配置生效（默认开）
    opsec::SetPathScrubbing(cfg_.anti_detect.strip_paths);

    int sw = 1920;
    int sh = 1080;
    ResolveScreenSize(cfg_, sw, sh);
    built_ = BuildSettings(cfg_, sw, sh);

    // 1) 先校验模型：输入尺寸是权威值，采集 ROI 需要据此确定
    const auto info = inference::InspectModel(model_path);
    if (!info.ok) {
        return Result<void>::Fail(4004, "模型校验失败: " + info.error,
                                  "用 --model 指定有效 ONNX，或确认 AIM_WITH_ORT=ON");
    }
    const int model_imgsz = (info.input.shape.size() == 4 && info.input.shape[2] > 0)
                                ? static_cast<int>(info.input.shape[2])
                                : built_.pipeline.imgsz;
    if (model_imgsz != built_.pipeline.imgsz) {
        AIM_ENGINE(Warn, "imgsz 不一致：config=" + std::to_string(built_.pipeline.imgsz) +
                             " model=" + std::to_string(model_imgsz) + "，以模型为准");
        built_.pipeline.imgsz = model_imgsz;
    }
    // ROI 小于模型输入时会被上采样：既不加信息，又让前处理走慢路径。
    // 自动抬到模型尺寸（只影响本次运行，不回写配置）。
    if (built_.capture_spec.roi_w > 0 && built_.capture_spec.roi_w < model_imgsz) {
        AIM_ENGINE(Warn, "ROI " + std::to_string(built_.capture_spec.roi_w) +
                             " < 模型输入 " + std::to_string(model_imgsz) + "，自动提升以避免上采样");
        built_.capture_spec.roi_w = model_imgsz;
        built_.capture_spec.roi_h = model_imgsz;
        built_.pipeline.capture_spec = built_.capture_spec;
    }

    // 2) 采集（§4.4 注册名由配置决定）
    own_capture_ = Registry::create<spi::ICapture>(built_.capture_name);
    if (!own_capture_) {
        return Result<void>::Fail(4001, "未知采集通道: " + built_.capture_name,
                                  "检查 capture.mode（dxgi|gdi|scrcpy）");
    }
    if (!own_capture_->configure(cfg_.capture)) {
        return Result<void>::Fail(4002, "采集通道配置失败: " + built_.capture_name,
                                  "检查 capture.scrcpy.serial 与设备连接");
    }
    if (!own_capture_->init(built_.capture_spec)) {
        return Result<void>::Fail(4003, "采集初始化失败: " + built_.capture_name,
                                  "确认桌面会话可用、显示器序号与分辨率正确");
    }

    // 采集与推理并行（§4.3）：采集跑独立线程写入双缓冲，推理只取最新帧。
    // 关闭 async_capture 时回退到同步 grab（旧路径，便于对照排查）。
    if (cfg_.inference.async_capture) {
        std::string pump_err;
        const int cap_fps = (cfg_.inference.target_fps > 0) ? cfg_.inference.target_fps : 0;
        if (pump_.Start(own_capture_.get(), built_.capture_spec, cap_fps, 8, &pump_err)) {
            pipeline_.SetFrameSource(&pump_.buffer());
            AIM_ENGINE(Info, "采集并行已启用（双缓冲，节拍 " + std::to_string(cap_fps) + " fps）");
        } else {
            AIM_ENGINE(Warn, "采集泵启动失败，回退同步采集: " + pump_err);
        }
    } else {
        AIM_ENGINE(Info, "采集并行已关闭（同步 grab）");
    }

    // 类别表以模型声明为准（schema v4）：回灌真实类别名，并按名解析白名单。
    // 旧方案用索引，跨模型必然错位（同一个 0 在不同模型里可能是 body/敌人/队友）。
    if (!info.classes.empty()) {
        built_.pipeline.class_names = info.classes;
        cfg_.inference.class_names = info.classes;
        const auto sel = ResolveClassWhitelist(cfg_.inference.selected_class_names, info.classes);
        built_.pipeline.class_whitelist = sel.indices;
        std::string names;
        for (size_t i = 0; i < sel.names.size(); ++i) {
            if (i) names += ", ";
            names += sel.names[i];
        }
        AIM_ENGINE(Info, "类别: " + std::to_string(info.classes.size()) + " 个；瞄准类别 = " +
                             (names.empty() ? "全部" : names) +
                             (sel.auto_selected ? "（自动挑选）" : "") +
                             (sel.warning.empty() ? "" : "；" + sel.warning));
    }

    // 3) 推理后端
    own_backend_ = Registry::create<spi::IBackend>(built_.backend_name);
    if (!own_backend_) {
        return Result<void>::Fail(4005, "未知推理后端: " + built_.backend_name,
                                  "检查 inference.backend（dml|cpu|tensorrt）");
    }
    spi::ModelSpec ms;
    ms.path = model_path;
    ms.imgsz = model_imgsz;
    ms.num_classes = info.num_classes;
    ms.num_kpts = info.num_kpts;
    ms.conf_threshold = built_.pipeline.conf_threshold;
    ms.input_nhwc_u8 = false;
    ms.threads = cfg_.inference.threads; // 0 = ORT 自动
    ms.zero_outputs = built_.zero_tensors; // M9 L2
    if (!own_backend_->load(ms)) {
        return Result<void>::Fail(4006, "模型加载失败: " + model_path,
                                  "DML 需 DirectML.dll>=1.13 与可用适配器（AIM_DML_DEVICE）");
    }

    // 3) 执行：dry_run 走 null 通道（不触碰真实输入）
    const std::string act_name = dry_run ? "null" : built_.actuator_name;
    own_actuator_ = Registry::create<spi::IActuator>(act_name);
    if (!own_actuator_) {
        return Result<void>::Fail(4007, "未知执行通道: " + act_name,
                                  "检查 actuator.channel（sendinput|ghub|kmbox|serial）");
    }
    if (!own_actuator_->configure(cfg_.actuator)) {
        return Result<void>::Fail(4008, "执行通道打开失败: " + act_name,
                                  "检查 KMBox IP/端口/UID、串口号、GHUB 设备路径");
    }

    // 4) 覆盖层（可选）
    if (with_overlay) {
        own_overlay_ = Registry::create<spi::IOverlay>("d3d11");
        if (!own_overlay_) {
            return Result<void>::Fail(4009, "覆盖层工厂不可用", "使用 --no-overlay 关闭覆盖层");
        }
    }

    capture_ = own_capture_.get();
    backend_ = own_backend_.get();
    actuator_ = own_actuator_.get();
    overlay_ = own_overlay_.get();

    pipeline_.Configure(PipelineDeps{capture_, backend_, actuator_, overlay_}, built_.pipeline);
    ConfigureWeaponAndHotkeys();

    // 与生产路径一致：开启 async_capture 时用采集泵接管帧源，
    // 让集成测试也能覆盖并行链路（关闭则走同步 grab）。
    if (cfg_.inference.async_capture && capture_) {
        std::string err;
        const int cap_fps = (cfg_.inference.target_fps > 0) ? cfg_.inference.target_fps : 0;
        if (pump_.Start(capture_, built_.capture_spec, cap_fps, 8, &err)) {
            pipeline_.SetFrameSource(&pump_.buffer());
        }
    }
    runtime_.Start(Clock::NowMs());
    runtime_.SetSegments(SegmentState::Running, SegmentState::Running,
                         dry_run ? SegmentState::Degraded : SegmentState::Running);
    applied_version_ = store_ ? store_->Version() : -1;
    return Result<void>::Ok();
}

void RunLoop::InitWithDeps(const Config& cfg, const BuiltSettings& built,
                           spi::ICapture* cap, spi::IBackend* be,
                           spi::IActuator* act, spi::IOverlay* ov, int frames_limit,
                           bool async) {
    cfg_ = cfg;
    built_ = built;
    frames_limit_ = frames_limit;
    capture_ = cap;
    backend_ = be;
    actuator_ = act;
    overlay_ = ov;
    pipeline_.Configure(PipelineDeps{capture_, backend_, actuator_, overlay_}, built_.pipeline);
    ConfigureWeaponAndHotkeys();

    // 与生产路径一致：开启 async_capture 时用采集泵接管帧源，
    // 让集成测试也能覆盖并行链路（关闭则走同步 grab）。
    if (async && capture_) {
        std::string err;
        const int cap_fps = (cfg_.inference.target_fps > 0) ? cfg_.inference.target_fps : 0;
        if (pump_.Start(capture_, built_.capture_spec, cap_fps, 8, &err)) {
            pipeline_.SetFrameSource(&pump_.buffer());
        }
    }
    runtime_.Start(Clock::NowMs());
    runtime_.SetSegments(SegmentState::Running, SegmentState::Running, SegmentState::Running);
    applied_version_ = store_ ? store_->Version() : -1;
}

void RunLoop::ReapplySettings() {
    if (!store_) return;
    cfg_ = store_->Get();
    applied_version_ = store_->Version();

    int sw = 1920;
    int sh = 1080;
    ResolveScreenSize(cfg_, sw, sh);
    BuiltSettings next = BuildSettings(cfg_, sw, sh);

    if (DeviceIdentityChanged(built_, next)) {
        // 采集通道/后端/ROI/imgsz 变更需要重新初始化设备：不热切，标记待重启
        restart_required_ = true;
        restart_reason_ = "采集通道/推理后端/ROI/imgsz 变更需重启引擎生效";
        return;
    }

    built_ = next;
    settings_dirty_ = true;
    ConfigureWeaponAndHotkeys();
}

void RunLoop::ConfigurePacer() {
    // 无目标降频：连续 10 帧没有目标后放宽到 anti_detect.idle_fps
    pacer_.Configure(cfg_.inference.target_fps, cfg_.anti_detect.idle_fps, 10);
}

void RunLoop::LoadRecoilPatterns() {
    LoadPatternsInto(cfg_, patterns_);
}

void RunLoop::ConfigureWeaponAndHotkeys() {
    // 跨游戏适配提示（§5.1.4）：profile 只做标识与风险提示，不改算法
    if (!cfg_.profile.game.empty()) {
        AIM_ENGINE(Info, "目标游戏: " + cfg_.profile.game + "（视角 " + cfg_.profile.view + "）");
    }
    if (cfg_.profile.view == "tpp") {
        AIM_ENGINE(Warn, "第三人称视角：准星与枪口不共线，请用 aim.crosshair_offset_x/y 校准");
    }

    // 自瞄激活状态机（§5.5.3）
    activation_.SetMode(cfg_.aim.activation_mode);
    activation_.Reset();

    // 无目标降频（§5.12.1）
    ConfigurePacer();

    // 压枪（§5.7.1）
    LoadRecoilPatterns();
    RecoilParams rp;
    rp.strength = cfg_.weapon.recoil.strength;
    rp.rpm = cfg_.weapon.recoil.rpm;
    rp.base_px = cfg_.weapon.recoil.base_px;
    rp.horizontal_gain = cfg_.weapon.recoil.horizontal_gain;
    rp.manual_horizontal = (cfg_.weapon.recoil.horizontal_source != "pattern");
    recoil_.Configure(rp, &patterns_, cfg_.weapon.recoil.pattern_id);

    // 扳机（§5.7.2）
    TriggerParams tp;
    tp.enabled = cfg_.weapon.trigger.enabled;
    tp.type = cfg_.weapon.trigger.type;
    tp.key = cfg_.weapon.trigger.key;
    tp.scope_follow_aim = cfg_.weapon.trigger.scope_follow_aim;
    tp.scope_half_w = cfg_.weapon.trigger.scope_half_w;
    tp.scope_half_h = cfg_.weapon.trigger.scope_half_h;
    tp.aim_offset_px = cfg_.weapon.trigger.aim_offset_px;
    tp.rifle_delay_ms = cfg_.weapon.trigger.rifle_delay_ms;
    tp.sniper_delay_ms = cfg_.weapon.trigger.sniper_delay_ms;
    tp.conf_threshold = cfg_.inference.conf_threshold;
    trigger_.Configure(tp);
    trigger_.Reset();

    alpha_ = cfg_.aim.smoothing.alpha;
    preset_ = cfg_.aim.target_point.preset;
    last_recoil_ms_ = 0;
    last_trigger_ms_ = 0;
}

void RunLoop::UpdateHotkeys(int64_t /*now_ms*/) {
    const auto pressed = [this](int vk) { return edges_.PressedState(vk, KeyDown(vk)); };

    // 1) 自瞄激活：主/副键合并（Hold/Toggle/Switch）
    const int vk_primary = VkFromName(cfg_.aim.hotkey_primary);
    const int vk_secondary = VkFromName(cfg_.aim.hotkey_secondary);
    armed_ = activation_.Update(KeyDown(vk_primary), KeyDown(vk_secondary));

    // 2) 吸附强度 ±（§5.5.2）
    if (pressed(VkFromName(cfg_.hotkeys.adhesion_plus))) {
        alpha_ = std::min(1.0f, alpha_ + 0.05f);
        built_.pipeline.aim.smoothing.alpha = alpha_;
        settings_dirty_ = true;
    }
    if (pressed(VkFromName(cfg_.hotkeys.adhesion_minus))) {
        alpha_ = std::max(0.05f, alpha_ - 0.05f);
        built_.pipeline.aim.smoothing.alpha = alpha_;
        settings_dirty_ = true;
    }

    // 3) 部位切换（↑/↓）
    static const TargetPreset kPresets[] = {TargetPreset::Head, TargetPreset::Neck,
                                            TargetPreset::Chest, TargetPreset::Abdomen,
                                            TargetPreset::Legs};
    constexpr int kN = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
    int idx = 0;
    for (int i = 0; i < kN; ++i) {
        if (kPresets[i] == preset_) { idx = i; break; }
    }
    if (pressed(VkFromName(cfg_.hotkeys.target_up))) {
        idx = (idx + kN - 1) % kN;
        preset_ = kPresets[idx];
        built_.pipeline.aim.target_point.preset = preset_;
        settings_dirty_ = true;
    }
    if (pressed(VkFromName(cfg_.hotkeys.target_down))) {
        idx = (idx + 1) % kN;
        preset_ = kPresets[idx];
        built_.pipeline.aim.target_point.preset = preset_;
        settings_dirty_ = true;
    }

    // 4) HUD 显隐（§5.9.1）
    if (pressed(VkFromName(cfg_.hotkeys.toggle_hud))) {
        hud_visible_ = !hud_visible_;
        if (overlay_) overlay_->set_visible(hud_visible_);
    }
}

void RunLoop::UpdateRecoil(int64_t now_ms) {
    recoil_px_ = Vec2F{};
    if (!cfg_.weapon.recoil.enabled) {
        recoil_.Reset();
        last_recoil_ms_ = now_ms;
        return;
    }

    // 开火判定：recoil.hotkey 为空时按左键（§5.7.1 默认跟随射击）
    const int vk = VkFromName(cfg_.weapon.recoil.hotkey);
    const bool firing = (vk != 0) ? KeyDown(vk) : KeyDown(0x01 /*VK_LBUTTON*/);
    if (!firing) {
        recoil_.Reset();
        last_recoil_ms_ = now_ms;
        return;
    }

    // 开镜强度：仅当配置了 strength_ads 且当前处于开镜状态时切换。
    // 这是纯查表（不按 FOV 换算），高倍镜该开多大由用户自己决定。
    const float want = (cfg_.fov.ads_zoom > 1.f && cfg_.weapon.recoil.strength_ads > 0.f)
                           ? cfg_.weapon.recoil.strength_ads
                           : cfg_.weapon.recoil.strength;
    if (want != recoil_strength_) {
        recoil_strength_ = want;
        recoil_.SetStrength(want);
    }

    if (last_recoil_ms_ > 0) {
        recoil_.Advance(static_cast<float>(now_ms - last_recoil_ms_));
    }
    last_recoil_ms_ = now_ms;
    recoil_px_ = recoil_.SampleNow(); // 正 = 下（补偿枪口上跳）
}

void RunLoop::UpdateTrigger(int64_t now_ms, const PipelineResult& r) {
    const float dt = (last_trigger_ms_ > 0)
                         ? static_cast<float>(now_ms - last_trigger_ms_)
                         : 0.f;
    last_trigger_ms_ = now_ms;

    if (!cfg_.weapon.trigger.enabled) return;
    const int vk = VkFromName(cfg_.weapon.trigger.key);
    if (vk == 0) return; // 未绑定 → 扳机不生效（§5.7.2）

    // 与管线同一准星基准（屏幕中心 + 每游戏偏移）
    const float cx = built_.pipeline.screen_w * 0.5f + cfg_.aim.crosshair_offset_x;
    const float cy = built_.pipeline.screen_h * 0.5f + cfg_.aim.crosshair_offset_y;
    bool hit = false;
    float score = 0.f;
    for (const auto& d : r.detections) {
        if (CrosshairHits(d, cx, cy, cfg_.weapon.trigger.aim_offset_px,
                          cfg_.inference.conf_threshold,
                          cfg_.weapon.trigger.hit_band_ratio)) {
            hit = true;
            score = std::max(score, d.score);
        }
    }

    const auto out = trigger_.Update(TriggerInput{KeyDown(vk), hit, score}, dt);
    if (out.fire_down) {
        spi::ClickCmd c;
        c.down = true;
        actuator_->click(c);
    }
    if (out.fire_up) {
        spi::ClickCmd c;
        c.down = false;
        actuator_->click(c);
    }
}

int RunLoop::Run() {
    if (!capture_ || !backend_ || !actuator_) return 4;

    stop_.store(false); // 允许复用同一对象再次 Run（测试/重启场景）
    RunCleanup cleanup{pump_, pipeline_, runtime_};

    // M9 L2（§5.12.2）：工作线程名中性化，避免 aim_* 这类可识别命名
    if (built_.neutral_thread_names) {
        opsec::ProcessGuard::SetNeutralThreadName("w0");
    }

    const int max_failures = MaxFailures(frames_limit_);
    last_frame_ms_ = Clock::NowMs(); // 看门狗起点：从未取到帧也要能超时
    const int64_t started = Clock::NowMs();
    int consecutive_failures = 0;

    while (!stop_.load()) {
        if (frames_limit_ > 0 && stats_.frames >= frames_limit_) break;

        if (store_ && store_->Version() != applied_version_) ReapplySettings();
        if (settings_dirty_) {
            pipeline_.ApplySettings(built_.pipeline);
            settings_dirty_ = false;
        }

        const int64_t frame_start = Clock::NowMs();

        UpdateHotkeys(frame_start);
        UpdateRecoil(frame_start);
        pipeline_.SetArmed(armed_);
        pipeline_.SetRecoilComp(recoil_px_);

        const auto r = pipeline_.ProcessFrame(frame_start);
        if (r.ok && !r.has_frame) {
            // 异步采集下本轮没有新帧：正常跳过，不推进统计。
            // 但若长时间一帧都取不到（采集源死掉/驱动异常），必须计入失败，
            // 否则循环会无限空转——这是并行化后新增的失效模式。
            constexpr int64_t kNoFrameTimeoutMs = 1000;
            if (last_frame_ms_ >= 0 && frame_start - last_frame_ms_ > kNoFrameTimeoutMs) {
                ++stats_.failures;
                ++consecutive_failures;
                if (max_failures > 0 && consecutive_failures >= max_failures) {
                    AIM_ENGINE(Error, "run aborted: 采集长时间无帧");
                    pump_.Stop();
                    pipeline_.SetFrameSource(nullptr);
                    return 7;
                }
            }
            if (on_frame) on_frame();
            continue;
        }
        if (r.ok) {
            ++stats_.frames;
            consecutive_failures = 0;
            last_frame_ms_ = frame_start;

            const auto c = backend_->counters();
            stats_.pre_ms_sum += c.pre_ms;
            stats_.infer_ms_sum += c.infer_ms;
            stats_.post_ms_sum += c.post_ms;

            FrameTimings t;
            t.cap_ms = 0.0;
            t.infer_ms = c.infer_ms;
            t.post_ms = c.post_ms;
            t.send_ms = 0.0;
            runtime_.OnFrame(frame_start, r, t);
            runtime_.SetDetections(r.detections);

            UpdateTrigger(frame_start, r);
        } else {
            ++stats_.failures;
            ++consecutive_failures;
            runtime_.OnDrop();
            if (max_failures > 0 && consecutive_failures >= max_failures) {
                AIM_ENGINE(Error, "run aborted: too many consecutive frame failures");
                pump_.Stop(); // 异常退出也要先停泵，避免线程在 capture 析构后继续跑
                pipeline_.SetFrameSource(nullptr);
                return 7;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        if (on_frame) on_frame();

        // 帧率节流（§4.3：target_fps 上限）+ 无目标自适应降频（§5.12.1）
        const int64_t spent = Clock::NowMs() - frame_start;
        int64_t budget = 1000 / std::max(1, built_.target_fps);
        const int idle_delay = pacer_.NextDelayMs(r.ok && r.has_target);
        if (idle_delay > budget) budget = idle_delay;
        if (spent < budget) {
            std::this_thread::sleep_for(std::chrono::milliseconds(budget - spent));
        }
    }

    // 清理交给 RunCleanup（析构时停泵 + 断帧源 + 停运行时）；这里先取统计再让它析构
    const auto ps = pump_.stats();
    if (ps.grabs > 0) {
        AIM_ENGINE(Info, "采集泵：抓帧 " + std::to_string(ps.grabs) + "，入队 " +
                             std::to_string(ps.submits) + "，空帧 " +
                             std::to_string(ps.empty) + "，拒收 " +
                             std::to_string(ps.rejected));
    }

    stats_.elapsed_ms = Clock::NowMs() - started;
    return 0;
}

} // namespace aim
