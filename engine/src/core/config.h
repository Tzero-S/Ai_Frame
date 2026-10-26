#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "core/result.h"
#include "core/json.h"

// 配置中心（§5.1）：单一真源，JSON Schema 驱动（schema_version=3）。
// 全部默认值以 §13.2 默认参数速查 为准；UI 与引擎共享同一份结构。
//
// 注：§5.1.2 示例、§13.2 默认参数速查、§5.8.2 查表已统一为 hfov 103°（16:9）；
// 本实现取 103.0，fov 模块按分辨率自动覆盖。vfov 59° 为存量默认，
// 自动值由公式 vfov=2·atan(tan(hfov/2)/aspect) 推出（16:9≈70.5°）。

namespace aim {

// ---- 枚举（字符串 ⇄ 枚举，便于 JSON 往返）----
enum class CaptureMode { Dxgi, Gdi, Scrcpy };
// 采集 ROI 模式（§8.2）：center_square | full（区别于瞄准范围 ScopeMode）
enum class RoiMode { CenterSquare, Full };
enum class Backend { Dml, Cpu, Tensorrt };
enum class ActivationMode { Hold, Toggle, Switch };
enum class ScopeMode { Rect, Circle, Fov };
enum class TargetPreset { Head, Neck, Chest, Abdomen, Legs, Custom };
enum class TargetSelection { NearestCrosshair, HighestScore, Sticky };
enum class ActuatorChannel { SendInput, Ghub, Kmbox, Serial };
enum class BezierType { EaseIn, EaseOut, Smoothing };
enum class AntiDetectLevel { Off, Standard, Strict };
enum class TriggerType { Rifle, Sniper };

const char* to_string(CaptureMode v);
const char* to_string(RoiMode v);
const char* to_string(Backend v);
const char* to_string(ActivationMode v);
const char* to_string(ScopeMode v);
const char* to_string(TargetPreset v);
const char* to_string(TargetSelection v);
const char* to_string(ActuatorChannel v);
const char* to_string(BezierType v);
const char* to_string(AntiDetectLevel v);
const char* to_string(TriggerType v);

bool from_string(std::string_view s, CaptureMode& out);
bool from_string(std::string_view s, RoiMode& out);
bool from_string(std::string_view s, Backend& out);
bool from_string(std::string_view s, ActivationMode& out);
bool from_string(std::string_view s, ScopeMode& out);
bool from_string(std::string_view s, TargetPreset& out);
bool from_string(std::string_view s, TargetSelection& out);
bool from_string(std::string_view s, ActuatorChannel& out);
bool from_string(std::string_view s, BezierType& out);
bool from_string(std::string_view s, AntiDetectLevel& out);
bool from_string(std::string_view s, TriggerType& out);

// ---- 分组结构（字段名与 §5.1.2 严格一致）----
struct CaptureConfig {
    CaptureMode mode = CaptureMode::Dxgi;
    struct Roi { RoiMode mode = RoiMode::CenterSquare; int edge = 640; } roi;
    int output_monitor = 0;
    struct GameWindow { std::string title_pattern; bool auto_detect = true; } game_window;
    struct Scrcpy { std::string serial; std::string host = "127.0.0.1";
                    int port = 27183; int edge = 640; } scrcpy;
};

struct InferenceConfig {
    Backend backend = Backend::Dml;
    int target_fps = 120;
    int thread_delay_ms = 0;
    // ORT intra-op 线程数：0 = 交给 ORT 自动（= 物理核数）。
    // 小模型（320 输入）下线程过多会让线程池空转，CPU 时间可降到 1/4。
    int threads = 0;
    // 采集与推理并行（双缓冲）：开启后采集跑独立线程，推理只取最新帧。
    // 关闭则回退到同步 grab（旧行为，便于对照排查）。
    bool async_capture = true;
    int imgsz = 640;
    float conf_threshold = 0.35f;
    float iou_threshold = 0.45f;
    // schema v4：类别按"名字"选择，避免跨模型索引错位。
    // 空 = 自动挑选瞄准类（body/head/敌人…，排除队友/倒地/靶场…）。
    std::vector<std::string> selected_class_names;
    // 最近一次加载的模型声明的真实类别（引擎回灌，供 UI 展示与下次解析）
    std::vector<std::string> class_names;
    std::string model_id;
};

struct AimConfig {
    std::string hotkey_primary = "MOUSE2";
    std::string hotkey_secondary = "MOUSE4";
    ActivationMode activation_mode = ActivationMode::Hold;
    struct Scope { ScopeMode mode = ScopeMode::Rect; int half_w = 320; int half_h = 240;
                   float radius_px = 0.f; float hfov_half = 0.f; float vfov_half = 0.f; } scope;
    struct TargetPoint { TargetPreset preset = TargetPreset::Head; float offset_y_ratio = 0.12f; } target_point;
    // 准星相对屏幕中心的偏移（px）。多数游戏为 0；准星不在几何中心的游戏需要配。
    float crosshair_offset_x = 0.f;
    float crosshair_offset_y = 0.f;
    struct MicroAdjust { int up = 0; int down = 0; int left = 0; int right = 0; int step_px = 2; } micro_adjust;
    struct RecoilMicro { bool enabled = true; float vertical = 0.f; float horizontal = 0.f;
                         std::string source = "pattern"; } recoil_micro;
    TargetSelection selection = TargetSelection::NearestCrosshair;
    struct Smoothing {
        bool enabled = true;
        std::string model = "critical"; // ema | critical（§5.5.4）
        float alpha = 0.35f;            // ema 模式
        float omega = 25.f;             // critical 模式：自然频率 rad/s
        float max_jerk = 0.f;           // critical 模式：jerk 上限 px/s³，0=不限
        float beta = 0.f;
        float lead = 0.f;
    } smoothing;
    int latency_compensation_ms = 0;
};

struct ActuatorConfig {
    ActuatorChannel channel = ActuatorChannel::SendInput;
    struct SendInputCfg { bool absolute = false; float move_scale = 1.0f; int min_step = 1; } sendinput;
    struct GhubCfg { std::string device_path =
                        "\\??\\ROOT#SYSTEM#0002#{1abc05c0-c378-41b9-9cef-df1aba82b015}";
                     std::string fallback_device = "\\??\\ROOT#SYSTEM#0001#{...}";
                     int max_step = 127; int hz = 1000; } ghub;
    struct KmboxCfg { std::string ip = "192.168.2.188"; int port = 8088;
                      std::string uid = "529C5C53";
                      struct Bezier { bool enabled = false; BezierType type = BezierType::EaseIn;
                                      float strength = 0.5f; } bezier; } kmbox;
    struct SerialCfg { std::string port = "COM1"; int baud = 115200; } serial;
};

struct WeaponConfig {
    // strength：0–30（步进 0.5）。15 = 峰值补偿等于 base_px；30 = 2×base_px。
    // 高倍镜需要更大补偿，因此上限放到 30，由用户自行判断要开多大。
    struct Recoil { bool enabled = true; float strength = 5.f; std::string hotkey; int rpm = 600;
                    std::string pattern_id = "m4a1s";
                    // 横向偏移：manual 时横向 = 垂直 × horizontal_gain（用户拖滑条）
                    // pattern 时使用曲线自带 dx。0 = 只压垂直。
                    float horizontal_gain = 0.f;          // [-1.5, 1.5]，负=向左
                    std::string horizontal_source = "manual";
                    float base_px = 200.f;                // 曲线峰值对应的总补偿像素
                    // 开镜强度（纯手动，0 = 跟随 strength）。仅当 fov.ads_zoom>1 时生效。
                    // 不做任何 FOV 自动换算——高倍镜该开多大由用户自己判断。
                    float strength_ads = 0.f;             // [0,30]
                    // 弹道库路径：相对路径先按当前目录找，再按可执行文件目录找。
                    // 空字符串 = 不加载（压枪退化为恒量垂直补偿）。
                    std::string patterns_path = "data/patterns.json"; } recoil;
    struct Trigger { bool enabled = false; TriggerType type = TriggerType::Rifle; std::string key;
                     bool scope_follow_aim = true; int scope_half_w = 320; int scope_half_h = 240;
                     int aim_offset_px = 0; int rifle_delay_ms = 70; int sniper_delay_ms = 0;
                     // 命中带纵向容差（框高比例）。动态准星扩散明显的游戏可调大。
                     float hit_band_ratio = 0.10f; } trigger;
};

struct FovConfig {
    bool auto_fov = true;
    float hfov_deg = 103.0f;   // §13.2 / §5.8.2（16:9 查表）；fov 模块自动覆盖
    float vfov_deg = 59.0f;    // 存量默认；自动值由 fov 模块公式推出（16:9≈70.5°）
    bool manual_override = false;

    // 开镜变焦表（§5.8.2）：zoom → 水平 FOV。开镜后 FOV 变小，像素↔角度换算必须跟着变。
    // 表为空表示不区分开镜（沿用 hfov_deg / 自动值）。
    struct AdsLevel {
        float zoom = 1.f;       // 倍率，1 = 腰射
        float hfov_deg = 103.f;
    };
    std::vector<AdsLevel> ads_levels;
    float ads_zoom = 1.f;       // 当前倍率（由配置或热键驱动，1 = 腰射）
};

struct HotkeysConfig {
    std::string toggle_hud = "F9";
    std::string toggle_team = "F5";
    std::string toggle_aim = "F4";
    std::string adhesion_minus = "F11";
    std::string adhesion_plus = "F12";
    std::string push_switch = "NUMPAD8";
    std::string target_up = "UP";
    std::string target_down = "DOWN";
    std::string target_left = "LEFT";
    std::string target_right = "RIGHT";
};

struct AntiDetectConfig {
    AntiDetectLevel level = AntiDetectLevel::Standard;
    bool hide_window = true;
    bool hide_on_focus_loss = true;
    bool randomize_timing = true;
    bool strip_paths = true;
    bool exclude_suspicious_modules = true;

    // L1 行为层（§5.12.1）
    float jitter_px = 0.8f;             // 高斯抖动 σ（px），0 = 关闭
    float timing_jitter_ratio = 0.10f;  // 步长/间隔 ±10%
    int switch_hysteresis_frames = 2;   // 目标切换迟滞帧数
    int idle_fps = 30;                  // 无目标时降频

    // L2 / L3（§5.12.2 / §5.12.4）
    bool anti_debug = false;            // 默认关闭（文档要求显式开启）
    bool zero_tensors = true;           // 张量内存用后清零
    bool neutral_thread_names = true;   // 线程名中性化
};

struct ProfileConfig {
    std::string name = "default";
    bool active = true;
    // 跨游戏适配（§5.1.4）：标识当前目标游戏与视角类型，仅影响日志/提示与后续扩展
    std::string game = "";      // 例如 valorant / cs2 / sjz2
    std::string view = "fpp";   // fpp | tpp（tpp 下准星与枪口不共线，需额外校准）
};

// ---- 顶层配置 ----
struct Config {
    int schema_version = 4;
    ProfileConfig profile;
    CaptureConfig capture;
    InferenceConfig inference;
    AimConfig aim;
    ActuatorConfig actuator;
    WeaponConfig weapon;
    FovConfig fov;
    HotkeysConfig hotkeys;
    AntiDetectConfig anti_detect;
};

// ---- 配置管理（加载/保存/迁移/校验/序列化）----
class ConfigManager {
public:
    // 内置默认值（§13.2）。等价于磁盘缺失时的生成结果。
    static Config Defaults();

    // 序列化 / 反序列化（§5.1.3 时序：解析 → 迁移 → 校验）
    static std::string ToJson(const Config& cfg, int indent = 2);
    static Result<Config> FromJson(const std::string& text);
    static Result<Config> LoadFromFile(const std::string& path);
    static Result<void> SaveToFile(const std::string& path, const Config& cfg);

    // 迁移：将任意 <=3 的旧版本补齐到 v3（缺字段用默认值合并，不破坏既有值）。
    // 返回最终生效的 schema_version。
    static int Migrate(json::Value& v);

    // 校验：schema_version 必须为 3，关键枚举合法（§8.2 子集）。
    static Result<void> Validate(const Config& cfg);
};

} // namespace aim
