#include "core/config.h"
#include "core/config_schema.h"
#include "core/json.h"
#include <cmath>
#include <cstring>
#include <fstream>

namespace aim {

// ============ 枚举 ⇄ 字符串 ============
const char* to_string(CaptureMode v) {
    switch (v) { case CaptureMode::Dxgi: return "dxgi";
                 case CaptureMode::Gdi: return "gdi";
                 case CaptureMode::Scrcpy: return "scrcpy"; }
    return "dxgi";
}
const char* to_string(RoiMode v) {
    switch (v) { case RoiMode::CenterSquare: return "center_square";
                 case RoiMode::Full: return "full"; }
    return "center_square";
}
const char* to_string(Backend v) {
    switch (v) { case Backend::Dml: return "dml";
                 case Backend::Cpu: return "cpu";
                 case Backend::Tensorrt: return "tensorrt"; }
    return "dml";
}
const char* to_string(ActivationMode v) {
    switch (v) { case ActivationMode::Hold: return "hold";
                 case ActivationMode::Toggle: return "toggle";
                 case ActivationMode::Switch: return "switch"; }
    return "hold";
}
const char* to_string(ScopeMode v) {
    switch (v) { case ScopeMode::Rect: return "rect";
                 case ScopeMode::Circle: return "circle";
                 case ScopeMode::Fov: return "fov"; }
    return "rect";
}
const char* to_string(TargetPreset v) {
    switch (v) { case TargetPreset::Head: return "head";
                 case TargetPreset::Neck: return "neck";
                 case TargetPreset::Chest: return "chest";
                 case TargetPreset::Abdomen: return "abdomen";
                 case TargetPreset::Legs: return "legs";
                 case TargetPreset::Custom: return "custom"; }
    return "head";
}
const char* to_string(TargetSelection v) {
    switch (v) { case TargetSelection::NearestCrosshair: return "nearest_crosshair";
                 case TargetSelection::HighestScore: return "highest_score";
                 case TargetSelection::Sticky: return "sticky"; }
    return "nearest_crosshair";
}
const char* to_string(ActuatorChannel v) {
    switch (v) { case ActuatorChannel::SendInput: return "sendinput";
                 case ActuatorChannel::Ghub: return "ghub";
                 case ActuatorChannel::Kmbox: return "kmbox";
                 case ActuatorChannel::Serial: return "serial"; }
    return "sendinput";
}
const char* to_string(BezierType v) {
    switch (v) { case BezierType::EaseIn: return "ease_in";
                 case BezierType::EaseOut: return "ease_out";
                 case BezierType::Smoothing: return "smoothing"; }
    return "ease_in";
}
const char* to_string(AntiDetectLevel v) {
    switch (v) { case AntiDetectLevel::Off: return "off";
                 case AntiDetectLevel::Standard: return "standard";
                 case AntiDetectLevel::Strict: return "strict"; }
    return "standard";
}
const char* to_string(TriggerType v) {
    switch (v) { case TriggerType::Rifle: return "rifle";
                 case TriggerType::Sniper: return "sniper"; }
    return "rifle";
}

#define AIM_FROM_ENUM(EnumType, enum_vals)                                    \
bool from_string(std::string_view s, EnumType& out) {                         \
    if (false) return false;                                                  \
    (void)s; (void)out;                                                       \
    return false;                                                            \
}
// 逐个实现（显式，便于静态可读）
bool from_string(std::string_view s, CaptureMode& out) {
    if (s == "dxgi") { out = CaptureMode::Dxgi; return true; }
    if (s == "gdi") { out = CaptureMode::Gdi; return true; }
    if (s == "scrcpy") { out = CaptureMode::Scrcpy; return true; }
    return false;
}
bool from_string(std::string_view s, RoiMode& out) {
    if (s == "center_square") { out = RoiMode::CenterSquare; return true; }
    if (s == "full") { out = RoiMode::Full; return true; }
    return false;
}
bool from_string(std::string_view s, Backend& out) {
    if (s == "dml") { out = Backend::Dml; return true; }
    if (s == "cpu") { out = Backend::Cpu; return true; }
    if (s == "tensorrt") { out = Backend::Tensorrt; return true; }
    return false;
}
bool from_string(std::string_view s, ActivationMode& out) {
    if (s == "hold") { out = ActivationMode::Hold; return true; }
    if (s == "toggle") { out = ActivationMode::Toggle; return true; }
    if (s == "switch") { out = ActivationMode::Switch; return true; }
    return false;
}
bool from_string(std::string_view s, ScopeMode& out) {
    if (s == "rect") { out = ScopeMode::Rect; return true; }
    if (s == "circle") { out = ScopeMode::Circle; return true; }
    if (s == "fov") { out = ScopeMode::Fov; return true; }
    return false;
}
bool from_string(std::string_view s, TargetPreset& out) {
    static const std::pair<std::string_view, TargetPreset> k[] = {
        {"head", TargetPreset::Head}, {"neck", TargetPreset::Neck},
        {"chest", TargetPreset::Chest}, {"abdomen", TargetPreset::Abdomen},
        {"legs", TargetPreset::Legs}, {"custom", TargetPreset::Custom}};
    for (auto& p : k) if (p.first == s) { out = p.second; return true; }
    return false;
}
bool from_string(std::string_view s, TargetSelection& out) {
    if (s == "nearest_crosshair") { out = TargetSelection::NearestCrosshair; return true; }
    if (s == "highest_score") { out = TargetSelection::HighestScore; return true; }
    if (s == "sticky") { out = TargetSelection::Sticky; return true; }
    return false;
}
bool from_string(std::string_view s, ActuatorChannel& out) {
    if (s == "sendinput") { out = ActuatorChannel::SendInput; return true; }
    if (s == "ghub") { out = ActuatorChannel::Ghub; return true; }
    if (s == "kmbox") { out = ActuatorChannel::Kmbox; return true; }
    if (s == "serial") { out = ActuatorChannel::Serial; return true; }
    return false;
}
bool from_string(std::string_view s, BezierType& out) {
    if (s == "ease_in") { out = BezierType::EaseIn; return true; }
    if (s == "ease_out") { out = BezierType::EaseOut; return true; }
    if (s == "smoothing") { out = BezierType::Smoothing; return true; }
    return false;
}
bool from_string(std::string_view s, AntiDetectLevel& out) {
    if (s == "off") { out = AntiDetectLevel::Off; return true; }
    if (s == "standard") { out = AntiDetectLevel::Standard; return true; }
    if (s == "strict") { out = AntiDetectLevel::Strict; return true; }
    return false;
}
bool from_string(std::string_view s, TriggerType& out) {
    if (s == "rifle") { out = TriggerType::Rifle; return true; }
    if (s == "sniper") { out = TriggerType::Sniper; return true; }
    return false;
}
#undef AIM_FROM_ENUM

// ============ 默认值 ============
Config ConfigManager::Defaults() {
    // 所有默认值已在 Config 结构体成员初始化中按 §13.2 设定，统一单一来源。
    return Config{};
}

// ============ 序列化 ============
namespace {
void put_str_arr(json::Value& v, const std::vector<std::string>& a) {
    v.reset(json::JType::Array);
    v.arr.reserve(a.size());
    for (const auto& s : a) v.push_back(json::Value(s));
}
} // namespace

std::string ConfigManager::ToJson(const Config& c, int indent) {
    json::Value v;
    v["schema_version"] = c.schema_version;

    auto& p = v["profile"];
    p["name"] = c.profile.name;
    p["active"] = c.profile.active;
    p["game"] = c.profile.game;
    p["view"] = c.profile.view;

    auto& cap = v["capture"];
    cap["mode"] = to_string(c.capture.mode);
    auto& roi = cap["roi"];
    roi["mode"] = to_string(c.capture.roi.mode);
    roi["edge"] = c.capture.roi.edge;
    cap["output_monitor"] = c.capture.output_monitor;
    auto& gw = cap["game_window"];
    gw["title_pattern"] = c.capture.game_window.title_pattern;
    gw["auto_detect"] = c.capture.game_window.auto_detect;
    auto& scr = cap["scrcpy"];
    scr["serial"] = c.capture.scrcpy.serial;
    scr["host"] = c.capture.scrcpy.host;
    scr["port"] = c.capture.scrcpy.port;
    scr["edge"] = c.capture.scrcpy.edge;

    auto& inf = v["inference"];
    inf["backend"] = to_string(c.inference.backend);
    inf["target_fps"] = c.inference.target_fps;
    inf["thread_delay_ms"] = c.inference.thread_delay_ms;
    inf["threads"] = c.inference.threads;
    inf["async_capture"] = c.inference.async_capture;
    inf["imgsz"] = c.inference.imgsz;
    inf["conf_threshold"] = c.inference.conf_threshold;
    inf["iou_threshold"] = c.inference.iou_threshold;
    put_str_arr(inf["selected_class_names"], c.inference.selected_class_names);
    inf["model_id"] = c.inference.model_id;
    put_str_arr(inf["class_names"], c.inference.class_names);

    auto& aim = v["aim"];
    aim["hotkey_primary"] = c.aim.hotkey_primary;
    aim["hotkey_secondary"] = c.aim.hotkey_secondary;
    aim["activation_mode"] = to_string(c.aim.activation_mode);
    auto& sc = aim["scope"];
    sc["mode"] = to_string(c.aim.scope.mode);
    sc["half_w"] = c.aim.scope.half_w;
    sc["half_h"] = c.aim.scope.half_h;
    sc["radius_px"] = c.aim.scope.radius_px;
    sc["hfov_half"] = c.aim.scope.hfov_half;
    sc["vfov_half"] = c.aim.scope.vfov_half;
    auto& tp = aim["target_point"];
    tp["preset"] = to_string(c.aim.target_point.preset);
    tp["offset_y_ratio"] = c.aim.target_point.offset_y_ratio;
    aim["crosshair_offset_x"] = c.aim.crosshair_offset_x;
    aim["crosshair_offset_y"] = c.aim.crosshair_offset_y;
    auto& ma = aim["micro_adjust"];
    ma["up"] = c.aim.micro_adjust.up;
    ma["down"] = c.aim.micro_adjust.down;
    ma["left"] = c.aim.micro_adjust.left;
    ma["right"] = c.aim.micro_adjust.right;
    ma["step_px"] = c.aim.micro_adjust.step_px;
    auto& rm = aim["recoil_micro"];
    rm["enabled"] = c.aim.recoil_micro.enabled;
    rm["vertical"] = c.aim.recoil_micro.vertical;
    rm["horizontal"] = c.aim.recoil_micro.horizontal;
    rm["source"] = c.aim.recoil_micro.source;
    aim["selection"] = to_string(c.aim.selection);
    auto& sm = aim["smoothing"];
    sm["enabled"] = c.aim.smoothing.enabled;
    sm["model"] = c.aim.smoothing.model;
    sm["alpha"] = c.aim.smoothing.alpha;
    sm["omega"] = c.aim.smoothing.omega;
    sm["max_jerk"] = c.aim.smoothing.max_jerk;
    sm["beta"] = c.aim.smoothing.beta;
    sm["lead"] = c.aim.smoothing.lead;
    aim["latency_compensation_ms"] = c.aim.latency_compensation_ms;

    auto& act = v["actuator"];
    act["channel"] = to_string(c.actuator.channel);
    auto& si = act["sendinput"];
    si["absolute"] = c.actuator.sendinput.absolute;
    si["move_scale"] = c.actuator.sendinput.move_scale;
    si["min_step"] = c.actuator.sendinput.min_step;
    auto& gh = act["ghub"];
    gh["device_path"] = c.actuator.ghub.device_path;
    gh["fallback_device"] = c.actuator.ghub.fallback_device;
    gh["max_step"] = c.actuator.ghub.max_step;
    gh["hz"] = c.actuator.ghub.hz;
    auto& km = act["kmbox"];
    km["ip"] = c.actuator.kmbox.ip;
    km["port"] = c.actuator.kmbox.port;
    km["uid"] = c.actuator.kmbox.uid;
    auto& kb = km["bezier"];
    kb["enabled"] = c.actuator.kmbox.bezier.enabled;
    kb["type"] = to_string(c.actuator.kmbox.bezier.type);
    kb["strength"] = c.actuator.kmbox.bezier.strength;
    auto& se = act["serial"];
    se["port"] = c.actuator.serial.port;
    se["baud"] = c.actuator.serial.baud;

    auto& wpn = v["weapon"];
    auto& rc = wpn["recoil"];
    rc["enabled"] = c.weapon.recoil.enabled;
    rc["strength"] = c.weapon.recoil.strength;
    rc["hotkey"] = c.weapon.recoil.hotkey;
    rc["rpm"] = c.weapon.recoil.rpm;
    rc["pattern_id"] = c.weapon.recoil.pattern_id;
    rc["patterns_path"] = c.weapon.recoil.patterns_path;
    rc["horizontal_gain"] = c.weapon.recoil.horizontal_gain;
    rc["horizontal_source"] = c.weapon.recoil.horizontal_source;
    rc["base_px"] = c.weapon.recoil.base_px;
    rc["strength_ads"] = c.weapon.recoil.strength_ads;
    auto& tr = wpn["trigger"];
    tr["enabled"] = c.weapon.trigger.enabled;
    tr["type"] = to_string(c.weapon.trigger.type);
    tr["key"] = c.weapon.trigger.key;
    tr["scope_follow_aim"] = c.weapon.trigger.scope_follow_aim;
    tr["scope_half_w"] = c.weapon.trigger.scope_half_w;
    tr["scope_half_h"] = c.weapon.trigger.scope_half_h;
    tr["aim_offset_px"] = c.weapon.trigger.aim_offset_px;
    tr["rifle_delay_ms"] = c.weapon.trigger.rifle_delay_ms;
    tr["sniper_delay_ms"] = c.weapon.trigger.sniper_delay_ms;
    tr["hit_band_ratio"] = c.weapon.trigger.hit_band_ratio;

    auto& fov = v["fov"];
    fov["auto"] = c.fov.auto_fov;
    fov["hfov_deg"] = c.fov.hfov_deg;
    fov["vfov_deg"] = c.fov.vfov_deg;
    fov["manual_override"] = c.fov.manual_override;
    fov["ads_zoom"] = c.fov.ads_zoom;
    {
        json::Value arr;
        arr.reset(json::JType::Array);
        for (const auto& lv : c.fov.ads_levels) {
            json::Value o;
            o["zoom"] = lv.zoom;
            o["hfov_deg"] = lv.hfov_deg;
            arr.push_back(o);
        }
        fov["ads_levels"] = arr;
    }

    auto& hk = v["hotkeys"];
    hk["toggle_hud"] = c.hotkeys.toggle_hud;
    hk["toggle_team"] = c.hotkeys.toggle_team;
    hk["toggle_aim"] = c.hotkeys.toggle_aim;
    hk["adhesion_minus"] = c.hotkeys.adhesion_minus;
    hk["adhesion_plus"] = c.hotkeys.adhesion_plus;
    hk["push_switch"] = c.hotkeys.push_switch;
    hk["target_up"] = c.hotkeys.target_up;
    hk["target_down"] = c.hotkeys.target_down;
    hk["target_left"] = c.hotkeys.target_left;
    hk["target_right"] = c.hotkeys.target_right;

    auto& ad = v["anti_detect"];
    ad["level"] = to_string(c.anti_detect.level);
    ad["hide_window"] = c.anti_detect.hide_window;
    ad["hide_on_focus_loss"] = c.anti_detect.hide_on_focus_loss;
    ad["randomize_timing"] = c.anti_detect.randomize_timing;
    ad["strip_paths"] = c.anti_detect.strip_paths;
    ad["exclude_suspicious_modules"] = c.anti_detect.exclude_suspicious_modules;
    ad["jitter_px"] = c.anti_detect.jitter_px;
    ad["timing_jitter_ratio"] = c.anti_detect.timing_jitter_ratio;
    ad["switch_hysteresis_frames"] = c.anti_detect.switch_hysteresis_frames;
    ad["idle_fps"] = c.anti_detect.idle_fps;
    ad["anti_debug"] = c.anti_detect.anti_debug;
    ad["zero_tensors"] = c.anti_detect.zero_tensors;
    ad["neutral_thread_names"] = c.anti_detect.neutral_thread_names;

    return json::dump(v, indent);
}

// ============ 反序列化 ============
namespace {
template <typename EnumT>
EnumT read_enum(const json::Value& v, EnumT def, bool (*fn)(std::string_view, EnumT&)) {
    EnumT out = def;
    if (!fn(v.asString(), out)) return def;
    return out;
}
} // namespace

Config ConfigFromValue(const json::Value& root) {
    Config c; // 内置默认值（§13.2）
    if (!root.isObject()) return c;

    c.schema_version = 4; // 迁移后统一为 4
    if (root["profile"].isObject()) {
        c.profile.name = root["profile"]["name"].asString(c.profile.name);
        c.profile.active = root["profile"]["active"].asBool(c.profile.active);
        c.profile.game = root["profile"]["game"].asString(c.profile.game);
        c.profile.view = root["profile"]["view"].asString(c.profile.view);
    }
    if (root["capture"].isObject()) {
        const auto& cap = root["capture"];
        c.capture.mode = read_enum(cap["mode"].asString(), c.capture.mode, from_string);
        if (cap["roi"].isObject()) {
            c.capture.roi.mode = read_enum(cap["roi"]["mode"].asString(), c.capture.roi.mode, from_string);
            c.capture.roi.edge = cap["roi"]["edge"].asInt(c.capture.roi.edge);
        }
        c.capture.output_monitor = cap["output_monitor"].asInt(c.capture.output_monitor);
        if (cap["game_window"].isObject()) {
            c.capture.game_window.title_pattern = cap["game_window"]["title_pattern"].asString(c.capture.game_window.title_pattern);
            c.capture.game_window.auto_detect = cap["game_window"]["auto_detect"].asBool(c.capture.game_window.auto_detect);
        }
        if (cap["scrcpy"].isObject()) {
            c.capture.scrcpy.serial = cap["scrcpy"]["serial"].asString(c.capture.scrcpy.serial);
            c.capture.scrcpy.host = cap["scrcpy"]["host"].asString(c.capture.scrcpy.host);
            c.capture.scrcpy.port = cap["scrcpy"]["port"].asInt(c.capture.scrcpy.port);
            c.capture.scrcpy.edge = cap["scrcpy"]["edge"].asInt(c.capture.scrcpy.edge);
        }
    }
    if (root["inference"].isObject()) {
        const auto& inf = root["inference"];
        c.inference.backend = read_enum(inf["backend"].asString(), c.inference.backend, from_string);
        c.inference.target_fps = inf["target_fps"].asInt(c.inference.target_fps);
        c.inference.thread_delay_ms = inf["thread_delay_ms"].asInt(c.inference.thread_delay_ms);
        c.inference.threads = inf["threads"].asInt(c.inference.threads);
        c.inference.async_capture = inf["async_capture"].asBool(c.inference.async_capture);
        c.inference.imgsz = inf["imgsz"].asInt(c.inference.imgsz);
        c.inference.conf_threshold = static_cast<float>(inf["conf_threshold"].asNumber(c.inference.conf_threshold));
        c.inference.iou_threshold = static_cast<float>(inf["iou_threshold"].asNumber(c.inference.iou_threshold));
        if (inf["selected_class_names"].isArray()) {
            c.inference.selected_class_names.clear();
            for (const auto& e : inf["selected_class_names"].arr)
                c.inference.selected_class_names.push_back(e.asString());
        } else if (inf["selected_classes"].isArray()) {
            // v3 兼容路径：索引 → 名字（用配置里记录的 class_names 映射）
            for (const auto& e : inf["selected_classes"].arr) {
                const int idx = e.asInt();
                if (idx >= 0 && idx < static_cast<int>(c.inference.class_names.size())) {
                    c.inference.selected_class_names.push_back(
                        c.inference.class_names[static_cast<size_t>(idx)]);
                }
            }
        }
        c.inference.model_id = inf["model_id"].asString(c.inference.model_id);
        if (inf["class_names"].isArray()) {
            c.inference.class_names.clear();
            for (const auto& e : inf["class_names"].arr) c.inference.class_names.push_back(e.asString());
        }
    }
    if (root["aim"].isObject()) {
        const auto& aim = root["aim"];
        c.aim.hotkey_primary = aim["hotkey_primary"].asString(c.aim.hotkey_primary);
        c.aim.hotkey_secondary = aim["hotkey_secondary"].asString(c.aim.hotkey_secondary);
        c.aim.activation_mode = read_enum(aim["activation_mode"].asString(), c.aim.activation_mode, from_string);
        if (aim["scope"].isObject()) {
            const auto& sc = aim["scope"];
            c.aim.scope.mode = read_enum(sc["mode"].asString(), c.aim.scope.mode, from_string);
            c.aim.scope.half_w = sc["half_w"].asInt(c.aim.scope.half_w);
            c.aim.scope.half_h = sc["half_h"].asInt(c.aim.scope.half_h);
            c.aim.scope.radius_px = static_cast<float>(sc["radius_px"].asNumber(c.aim.scope.radius_px));
            c.aim.scope.hfov_half = static_cast<float>(sc["hfov_half"].asNumber(c.aim.scope.hfov_half));
            c.aim.scope.vfov_half = static_cast<float>(sc["vfov_half"].asNumber(c.aim.scope.vfov_half));
        }
        c.aim.crosshair_offset_x =
            static_cast<float>(aim["crosshair_offset_x"].asNumber(c.aim.crosshair_offset_x));
        c.aim.crosshair_offset_y =
            static_cast<float>(aim["crosshair_offset_y"].asNumber(c.aim.crosshair_offset_y));
        if (aim["target_point"].isObject()) {
            c.aim.target_point.preset = read_enum(aim["target_point"]["preset"].asString(), c.aim.target_point.preset, from_string);
            c.aim.target_point.offset_y_ratio = static_cast<float>(aim["target_point"]["offset_y_ratio"].asNumber(c.aim.target_point.offset_y_ratio));
        }
        if (aim["micro_adjust"].isObject()) {
            const auto& ma = aim["micro_adjust"];
            c.aim.micro_adjust.up = ma["up"].asInt(c.aim.micro_adjust.up);
            c.aim.micro_adjust.down = ma["down"].asInt(c.aim.micro_adjust.down);
            c.aim.micro_adjust.left = ma["left"].asInt(c.aim.micro_adjust.left);
            c.aim.micro_adjust.right = ma["right"].asInt(c.aim.micro_adjust.right);
            c.aim.micro_adjust.step_px = ma["step_px"].asInt(c.aim.micro_adjust.step_px);
        }
        if (aim["recoil_micro"].isObject()) {
            const auto& rm = aim["recoil_micro"];
            c.aim.recoil_micro.enabled = rm["enabled"].asBool(c.aim.recoil_micro.enabled);
            c.aim.recoil_micro.vertical = static_cast<float>(rm["vertical"].asNumber(c.aim.recoil_micro.vertical));
            c.aim.recoil_micro.horizontal = static_cast<float>(rm["horizontal"].asNumber(c.aim.recoil_micro.horizontal));
            c.aim.recoil_micro.source = rm["source"].asString(c.aim.recoil_micro.source);
        }
        c.aim.selection = read_enum(aim["selection"].asString(), c.aim.selection, from_string);
        if (aim["smoothing"].isObject()) {
            const auto& sm = aim["smoothing"];
            c.aim.smoothing.enabled = sm["enabled"].asBool(c.aim.smoothing.enabled);
            c.aim.smoothing.model = sm["model"].asString(c.aim.smoothing.model);
            c.aim.smoothing.alpha = static_cast<float>(sm["alpha"].asNumber(c.aim.smoothing.alpha));
            c.aim.smoothing.omega = static_cast<float>(sm["omega"].asNumber(c.aim.smoothing.omega));
            c.aim.smoothing.max_jerk =
                static_cast<float>(sm["max_jerk"].asNumber(c.aim.smoothing.max_jerk));
            c.aim.smoothing.beta = static_cast<float>(sm["beta"].asNumber(c.aim.smoothing.beta));
            c.aim.smoothing.lead = static_cast<float>(sm["lead"].asNumber(c.aim.smoothing.lead));
        }
        c.aim.latency_compensation_ms = aim["latency_compensation_ms"].asInt(c.aim.latency_compensation_ms);
    }
    if (root["actuator"].isObject()) {
        const auto& act = root["actuator"];
        c.actuator.channel = read_enum(act["channel"].asString(), c.actuator.channel, from_string);
        if (act["sendinput"].isObject()) {
            const auto& si = act["sendinput"];
            c.actuator.sendinput.absolute = si["absolute"].asBool(c.actuator.sendinput.absolute);
            c.actuator.sendinput.move_scale = static_cast<float>(si["move_scale"].asNumber(c.actuator.sendinput.move_scale));
            c.actuator.sendinput.min_step = si["min_step"].asInt(c.actuator.sendinput.min_step);
        }
        if (act["ghub"].isObject()) {
            const auto& gh = act["ghub"];
            c.actuator.ghub.device_path = gh["device_path"].asString(c.actuator.ghub.device_path);
            c.actuator.ghub.fallback_device = gh["fallback_device"].asString(c.actuator.ghub.fallback_device);
            c.actuator.ghub.max_step = gh["max_step"].asInt(c.actuator.ghub.max_step);
            c.actuator.ghub.hz = gh["hz"].asInt(c.actuator.ghub.hz);
        }
        if (act["kmbox"].isObject()) {
            const auto& km = act["kmbox"];
            c.actuator.kmbox.ip = km["ip"].asString(c.actuator.kmbox.ip);
            c.actuator.kmbox.port = km["port"].asInt(c.actuator.kmbox.port);
            c.actuator.kmbox.uid = km["uid"].asString(c.actuator.kmbox.uid);
            if (km["bezier"].isObject()) {
                const auto& kb = km["bezier"];
                c.actuator.kmbox.bezier.enabled = kb["enabled"].asBool(c.actuator.kmbox.bezier.enabled);
                c.actuator.kmbox.bezier.type = read_enum(kb["type"].asString(), c.actuator.kmbox.bezier.type, from_string);
                c.actuator.kmbox.bezier.strength = static_cast<float>(kb["strength"].asNumber(c.actuator.kmbox.bezier.strength));
            }
        }
        if (act["serial"].isObject()) {
            c.actuator.serial.port = act["serial"]["port"].asString(c.actuator.serial.port);
            c.actuator.serial.baud = act["serial"]["baud"].asInt(c.actuator.serial.baud);
        }
    }
    if (root["weapon"].isObject()) {
        const auto& wpn = root["weapon"];
        if (wpn["recoil"].isObject()) {
            const auto& rc = wpn["recoil"];
            c.weapon.recoil.enabled = rc["enabled"].asBool(c.weapon.recoil.enabled);
            c.weapon.recoil.strength =
                static_cast<float>(rc["strength"].asNumber(c.weapon.recoil.strength));
            c.weapon.recoil.hotkey = rc["hotkey"].asString(c.weapon.recoil.hotkey);
            c.weapon.recoil.rpm = rc["rpm"].asInt(c.weapon.recoil.rpm);
            c.weapon.recoil.pattern_id = rc["pattern_id"].asString(c.weapon.recoil.pattern_id);
            c.weapon.recoil.patterns_path =
                rc["patterns_path"].asString(c.weapon.recoil.patterns_path);
            c.weapon.recoil.horizontal_gain = static_cast<float>(
                rc["horizontal_gain"].asNumber(c.weapon.recoil.horizontal_gain));
            c.weapon.recoil.horizontal_source =
                rc["horizontal_source"].asString(c.weapon.recoil.horizontal_source);
            c.weapon.recoil.base_px =
                static_cast<float>(rc["base_px"].asNumber(c.weapon.recoil.base_px));
            c.weapon.recoil.strength_ads = static_cast<float>(
                rc["strength_ads"].asNumber(c.weapon.recoil.strength_ads));
        }
        if (wpn["trigger"].isObject()) {
            const auto& tr = wpn["trigger"];
            c.weapon.trigger.enabled = tr["enabled"].asBool(c.weapon.trigger.enabled);
            c.weapon.trigger.type = read_enum(tr["type"].asString(), c.weapon.trigger.type, from_string);
            c.weapon.trigger.key = tr["key"].asString(c.weapon.trigger.key);
            c.weapon.trigger.scope_follow_aim = tr["scope_follow_aim"].asBool(c.weapon.trigger.scope_follow_aim);
            c.weapon.trigger.scope_half_w = tr["scope_half_w"].asInt(c.weapon.trigger.scope_half_w);
            c.weapon.trigger.scope_half_h = tr["scope_half_h"].asInt(c.weapon.trigger.scope_half_h);
            c.weapon.trigger.aim_offset_px = tr["aim_offset_px"].asInt(c.weapon.trigger.aim_offset_px);
            c.weapon.trigger.rifle_delay_ms = tr["rifle_delay_ms"].asInt(c.weapon.trigger.rifle_delay_ms);
            c.weapon.trigger.sniper_delay_ms = tr["sniper_delay_ms"].asInt(c.weapon.trigger.sniper_delay_ms);
            c.weapon.trigger.hit_band_ratio =
                static_cast<float>(tr["hit_band_ratio"].asNumber(c.weapon.trigger.hit_band_ratio));
        }
    }
    if (root["fov"].isObject()) {
        const auto& fov = root["fov"];
        c.fov.auto_fov = fov["auto"].asBool(c.fov.auto_fov);
        c.fov.hfov_deg = static_cast<float>(fov["hfov_deg"].asNumber(c.fov.hfov_deg));
        c.fov.vfov_deg = static_cast<float>(fov["vfov_deg"].asNumber(c.fov.vfov_deg));
        c.fov.manual_override = fov["manual_override"].asBool(c.fov.manual_override);
        c.fov.ads_zoom = static_cast<float>(fov["ads_zoom"].asNumber(c.fov.ads_zoom));
        if (fov["ads_levels"].isArray()) {
            c.fov.ads_levels.clear();
            for (const auto& lv : fov["ads_levels"].arr) {
                FovConfig::AdsLevel a;
                a.zoom = static_cast<float>(lv["zoom"].asNumber(1.0));
                a.hfov_deg = static_cast<float>(lv["hfov_deg"].asNumber(a.hfov_deg));
                c.fov.ads_levels.push_back(a);
            }
        }
    }
    if (root["hotkeys"].isObject()) {
        const auto& hk = root["hotkeys"];
        c.hotkeys.toggle_hud = hk["toggle_hud"].asString(c.hotkeys.toggle_hud);
        c.hotkeys.toggle_team = hk["toggle_team"].asString(c.hotkeys.toggle_team);
        c.hotkeys.toggle_aim = hk["toggle_aim"].asString(c.hotkeys.toggle_aim);
        c.hotkeys.adhesion_minus = hk["adhesion_minus"].asString(c.hotkeys.adhesion_minus);
        c.hotkeys.adhesion_plus = hk["adhesion_plus"].asString(c.hotkeys.adhesion_plus);
        c.hotkeys.push_switch = hk["push_switch"].asString(c.hotkeys.push_switch);
        c.hotkeys.target_up = hk["target_up"].asString(c.hotkeys.target_up);
        c.hotkeys.target_down = hk["target_down"].asString(c.hotkeys.target_down);
        c.hotkeys.target_left = hk["target_left"].asString(c.hotkeys.target_left);
        c.hotkeys.target_right = hk["target_right"].asString(c.hotkeys.target_right);
    }
    if (root["anti_detect"].isObject()) {
        const auto& ad = root["anti_detect"];
        c.anti_detect.level = read_enum(ad["level"].asString(), c.anti_detect.level, from_string);
        c.anti_detect.hide_window = ad["hide_window"].asBool(c.anti_detect.hide_window);
        c.anti_detect.hide_on_focus_loss = ad["hide_on_focus_loss"].asBool(c.anti_detect.hide_on_focus_loss);
        c.anti_detect.randomize_timing = ad["randomize_timing"].asBool(c.anti_detect.randomize_timing);
        c.anti_detect.strip_paths = ad["strip_paths"].asBool(c.anti_detect.strip_paths);
        c.anti_detect.exclude_suspicious_modules = ad["exclude_suspicious_modules"].asBool(c.anti_detect.exclude_suspicious_modules);
        c.anti_detect.jitter_px = static_cast<float>(ad["jitter_px"].asNumber(c.anti_detect.jitter_px));
        c.anti_detect.timing_jitter_ratio = static_cast<float>(ad["timing_jitter_ratio"].asNumber(c.anti_detect.timing_jitter_ratio));
        c.anti_detect.switch_hysteresis_frames = ad["switch_hysteresis_frames"].asInt(c.anti_detect.switch_hysteresis_frames);
        c.anti_detect.idle_fps = ad["idle_fps"].asInt(c.anti_detect.idle_fps);
        c.anti_detect.anti_debug = ad["anti_debug"].asBool(c.anti_detect.anti_debug);
        c.anti_detect.zero_tensors = ad["zero_tensors"].asBool(c.anti_detect.zero_tensors);
        c.anti_detect.neutral_thread_names = ad["neutral_thread_names"].asBool(c.anti_detect.neutral_thread_names);
    }
    return c;
}

// ============ 合并默认值（迁移核心）============
namespace {
void MergeInto(json::Value& dst, const json::Value& def) {
    if (!def.isObject()) return;
    for (const auto& kv : def.obj) {
        json::Value* d = dst.find(kv.first);
        if (!d) {
            dst[kv.first] = kv.second; // 缺字段 → 用默认值补齐
        } else if (d->isObject() && kv.second.isObject()) {
            MergeInto(*d, kv.second);   // 递归合并子对象
        }
        // 数组/标量：保留既有值
    }
}
} // namespace

int ConfigManager::Migrate(json::Value& v) {
    // 以默认模板递归补齐缺失字段（覆盖 v1/v2/v3/v4 任意旧版）
    json::Value def;
    std::string err;
    json::parse(ToJson(Defaults()), def, err); // 由默认值构造默认模板
    const int from = v["schema_version"].asInt(0);
    MergeInto(v, def);
    // v2→v3：确保 actuator.channel 存在（迁移链上的关键新增分组）
    if (!v["actuator"].isObject()) v["actuator"] = json::Value{};
    if (!v["actuator"].find("channel")) v["actuator"]["channel"] = "sendinput";

    // v3→v4：inference.selected_classes（索引）→ selected_class_names（名字）。
    // 旧配置里的 class_names 只是占位默认值（head/body），但按它映射仍比直接丢弃更接近原意；
    // 若映射不出任何名字，则留空（= 加载时自动挑选），由引擎在加载模型时再解析。
    if (from > 0 && from < 4) {
        auto& inf = v["inference"];
        if (inf.isObject()) {
            const json::Value sel = inf["selected_classes"];
            const json::Value names = inf["class_names"];
            json::Value out;
            out.reset(json::JType::Array);
            if (sel.isArray() && names.isArray()) {
                for (const auto& e : sel.arr) {
                    const int idx = e.asInt();
                    if (idx >= 0 && idx < static_cast<int>(names.arr.size())) {
                        out.push_back(json::Value(names.arr[static_cast<size_t>(idx)].asString()));
                    }
                }
            }
            inf["selected_class_names"] = out;
            // 旧键不主动删除：ConfigFromValue 优先读 selected_class_names，
            // 下次 ToJson 落盘时旧键自然消失。
        }
    }

    v["schema_version"] = 4;
    return 4;
}

Result<Config> ConfigManager::FromJson(const std::string& text) {
    json::Value v;
    std::string err;
    if (!json::parse(text, v, err))
        return Result<Config>::Fail(1001, "配置 JSON 解析失败: " + err,
                                    "检查配置文件是否为合法 JSON，或删除后以默认值重启");
    Migrate(v);
    Config c = ConfigFromValue(v);
    auto valid = Validate(c);
    if (!valid.ok()) return Result<Config>::Fail(valid.error().code, valid.error().message, valid.error().advice);
    return Result<Config>::Ok(std::move(c));
}

Result<void> ConfigManager::Validate(const Config& c) {
    // 统一失败出口：code 从 1002 起递增，message 说明"哪个字段不对"，advice 说明"怎么改"
    auto bad = [](int code, const std::string& msg, const std::string& advice) {
        return Result<void>::Fail(code, msg, advice);
    };
    // 浮点先查有限性，避免 NaN 绕过所有区间判断
    auto finite = [](float v) { return std::isfinite(v); };

    if (c.schema_version != 4)
        return bad(1002, "schema_version 必须为 4", "请通过迁移器升级配置，不要手动修改版本号");

    // ---- 推理 ----
    if (c.inference.imgsz < 32 || c.inference.imgsz > 4096)
        return bad(1003, "inference.imgsz 超出 [32,4096]", "模型输入边长通常为 320/416/640/960");
    if (!finite(c.inference.conf_threshold) || c.inference.conf_threshold < 0.01f ||
        c.inference.conf_threshold > 0.99f)
        return bad(1004, "conf_threshold 超出 [0.01,0.99]", "调整推理置信度阈值");
    if (!finite(c.inference.iou_threshold) || c.inference.iou_threshold < 0.01f ||
        c.inference.iou_threshold > 0.99f)
        return bad(1005, "inference.iou_threshold 超出 [0.01,0.99]", "NMS IoU 阈值通常 0.4~0.6");
    if (c.inference.target_fps < 1 || c.inference.target_fps > 1000)
        return bad(1006, "inference.target_fps 超出 [1,1000]", "常见 60/120/144/240");
    if (c.inference.thread_delay_ms < 0 || c.inference.thread_delay_ms > 1000)
        return bad(1007, "inference.thread_delay_ms 超出 [0,1000]", "留空或设为 0");
    if (c.inference.threads < 0 || c.inference.threads > 128)
        return bad(1034, "inference.threads 超出 [0,128]", "0 = ORT 自动；小模型建议 4~8");

    // ---- 瞄准 ----
    if (c.aim.scope.half_w < 0 || c.aim.scope.half_h < 0)
        return bad(1008, "aim.scope 半宽/半高不能为负", "检查 ROI 配置");
    if (c.aim.scope.half_w > 20000.f || c.aim.scope.half_h > 20000.f)
        return bad(1009, "aim.scope 半宽/半高过大（>20000）", "瞄准范围不应超过屏幕尺寸");
    if (!finite(c.aim.scope.radius_px) || c.aim.scope.radius_px < 0.f)
        return bad(1010, "aim.scope.radius_px 必须为非负有限值", "圆形范围模式下半径需 > 0");
    if (!finite(c.aim.crosshair_offset_x) || !finite(c.aim.crosshair_offset_y) ||
        std::fabs(c.aim.crosshair_offset_x) > 10000.f ||
        std::fabs(c.aim.crosshair_offset_y) > 10000.f)
        return bad(1011, "aim.crosshair_offset_x/y 非法", "准星偏移应远小于屏幕尺寸，通常为 0");
    if (!finite(c.aim.target_point.offset_y_ratio) || c.aim.target_point.offset_y_ratio < 0.f ||
        c.aim.target_point.offset_y_ratio > 1.f)
        return bad(1012, "aim.target_point.offset_y_ratio 超出 [0,1]", "框内比例，头 0.12 / 胸 0.35");
    if (c.aim.latency_compensation_ms < 0 || c.aim.latency_compensation_ms > 1000)
        return bad(1013, "aim.latency_compensation_ms 超出 [0,1000]", "按实测端到端延迟填写");

    // 平滑（§5.5.4）
    if (c.aim.smoothing.model != "ema" && c.aim.smoothing.model != "critical")
        return bad(1014, "aim.smoothing.model 只能为 ema 或 critical", "二选一");
    if (!finite(c.aim.smoothing.alpha) || c.aim.smoothing.alpha <= 0.f ||
        c.aim.smoothing.alpha > 1.f)
        return bad(1015, "aim.smoothing.alpha 超出 (0,1]", "越小越平滑");
    if (!finite(c.aim.smoothing.omega) || c.aim.smoothing.omega <= 0.f ||
        c.aim.smoothing.omega > 200.f)
        return bad(1016, "aim.smoothing.omega 超出 (0,200]", "越大越跟手，常用 15~40");
    if (!finite(c.aim.smoothing.max_jerk) || c.aim.smoothing.max_jerk < 0.f)
        return bad(1017, "aim.smoothing.max_jerk 必须为非负有限值", "0 表示不限幅");

    // ---- 视野 ----
    if (!finite(c.fov.hfov_deg) || c.fov.hfov_deg <= 0.f || c.fov.hfov_deg >= 180.f)
        return bad(1018, "fov.hfov_deg 超出 (0,180)", "16:9 常见 103°");
    if (!finite(c.fov.vfov_deg) || c.fov.vfov_deg <= 0.f || c.fov.vfov_deg >= 180.f)
        return bad(1019, "fov.vfov_deg 超出 (0,180)", "16:9 常见 70.5°");
    if (!finite(c.fov.ads_zoom) || c.fov.ads_zoom < 1.f || c.fov.ads_zoom > 64.f)
        return bad(1020, "fov.ads_zoom 超出 [1,64]", "1 表示腰射");
    for (const auto& lv : c.fov.ads_levels) {
        if (!finite(lv.zoom) || lv.zoom < 1.f || lv.zoom > 64.f)
            return bad(1021, "fov.ads_levels[].zoom 超出 [1,64]", "倍率必须 ≥ 1");
        if (!finite(lv.hfov_deg) || lv.hfov_deg <= 0.f || lv.hfov_deg >= 180.f)
            return bad(1022, "fov.ads_levels[].hfov_deg 超出 (0,180)", "开镜后 FOV 应变小");
    }

    // ---- 采集 ----
    if (c.capture.roi.edge < 1 || c.capture.roi.edge > 20000)
        return bad(1023, "capture.roi.edge 超出 [1,20000]", "ROI 边长，常见 320~960");

    // ---- 武器 ----
    if (!finite(c.weapon.recoil.strength) || c.weapon.recoil.strength < 0.f ||
        c.weapon.recoil.strength > 30.f)
        return bad(1024, "weapon.recoil.strength 超出 [0,30]", "15 = 峰值等于 base_px；30 = 2 倍");
    if (!finite(c.weapon.recoil.horizontal_gain) ||
        std::fabs(c.weapon.recoil.horizontal_gain) > 1.5f)
        return bad(1035, "weapon.recoil.horizontal_gain 超出 [-1.5,1.5]", "0=只压垂直，负=向左");
    if (c.weapon.recoil.horizontal_source != "manual" &&
        c.weapon.recoil.horizontal_source != "pattern")
        return bad(1036, "weapon.recoil.horizontal_source 只能为 manual 或 pattern", "二选一");
    if (!finite(c.weapon.recoil.strength_ads) || c.weapon.recoil.strength_ads < 0.f ||
        c.weapon.recoil.strength_ads > 30.f)
        return bad(1038, "weapon.recoil.strength_ads 超出 [0,30]", "0 = 跟随腰射强度");
    // 上限 6000：覆盖 12 倍镜所需的 2400~4000px（实测 4 倍镜约需 5.4 倍腰射量）
    if (!finite(c.weapon.recoil.base_px) || c.weapon.recoil.base_px < 0.f ||
        c.weapon.recoil.base_px > 6000.f)
        return bad(1037, "weapon.recoil.base_px 超出 [0,6000]",
                   "曲线峰值补偿像素；腰射常用 100~400，高倍镜可到数千");
    if (c.weapon.recoil.rpm < 1 || c.weapon.recoil.rpm > 3000)
        return bad(1025, "weapon.recoil.rpm 超出 [1,3000]", "武器射速，常见 600/750/900");
    if (!finite(c.weapon.trigger.hit_band_ratio) || c.weapon.trigger.hit_band_ratio <= 0.f ||
        c.weapon.trigger.hit_band_ratio > 1.f)
        return bad(1026, "weapon.trigger.hit_band_ratio 超出 (0,1]", "框高比例，常用 0.05~0.25");
    if (c.weapon.trigger.rifle_delay_ms < 0 || c.weapon.trigger.rifle_delay_ms > 5000 ||
        c.weapon.trigger.sniper_delay_ms < 0 || c.weapon.trigger.sniper_delay_ms > 5000)
        return bad(1027, "weapon.trigger 延迟超出 [0,5000]ms", "狙击通常 0，步枪 30~100");
    if (c.weapon.trigger.scope_half_w < 0 || c.weapon.trigger.scope_half_h < 0)
        return bad(1028, "weapon.trigger.scope 半宽/半高不能为负", "检查扳机范围");

    // ---- 执行 ----
    if (c.actuator.kmbox.port < 1 || c.actuator.kmbox.port > 65535)
        return bad(1029, "actuator.kmbox.port 超出 [1,65535]", "默认 8088");

    // ---- 反检测 ----
    if (c.anti_detect.idle_fps < 1 || c.anti_detect.idle_fps > 1000)
        return bad(1030, "anti_detect.idle_fps 超出 [1,1000]", "无目标降频，建议 ≤ target_fps");
    if (!finite(c.anti_detect.jitter_px) || c.anti_detect.jitter_px < 0.f ||
        c.anti_detect.jitter_px > 50.f)
        return bad(1031, "anti_detect.jitter_px 超出 [0,50]", "抖动幅度，0 表示关闭");
    if (!finite(c.anti_detect.timing_jitter_ratio) ||
        c.anti_detect.timing_jitter_ratio < 0.f || c.anti_detect.timing_jitter_ratio > 1.f)
        return bad(1032, "anti_detect.timing_jitter_ratio 超出 [0,1]", "时间抖动比例");
    if (c.anti_detect.switch_hysteresis_frames < 0 ||
        c.anti_detect.switch_hysteresis_frames > 60)
        return bad(1033, "anti_detect.switch_hysteresis_frames 超出 [0,60]", "切换迟滞帧数");

    // ---- 采集目标屏 ----
    // 不校验的后果：配错序号只会在运行期退化成主屏并打一条 WARN，用户很难关联到"配错了"。
    if (c.capture.output_monitor < 0 || c.capture.output_monitor > 15)
        return bad(1039, "capture.output_monitor 超出 [0,15]",
                   "与 DXGI 输出序号一致，0 = 第一块屏");

    // ---- 执行通道细节 ----
    if (!finite(c.actuator.sendinput.move_scale) || c.actuator.sendinput.move_scale <= 0.f ||
        c.actuator.sendinput.move_scale > 100.f)
        return bad(1040, "actuator.sendinput.move_scale 超出 (0,100]",
                   "1 = 原始像素；0 会导致完全不动");
    if (c.actuator.sendinput.min_step < 0 || c.actuator.sendinput.min_step > 1000)
        return bad(1041, "actuator.sendinput.min_step 超出 [0,1000]", "最小步进像素");
    if (c.actuator.ghub.hz < 1 || c.actuator.ghub.hz > 8000)
        return bad(1042, "actuator.ghub.hz 超出 [1,8000]", "上报频率，常见 1000");
    if (c.actuator.ghub.max_step < 1 || c.actuator.ghub.max_step > 127)
        return bad(1043, "actuator.ghub.max_step 超出 [1,127]", "单报告步进上限");
    if (!ConfigSchema::IsIpv4(c.actuator.kmbox.ip))
        return bad(1044, "actuator.kmbox.ip 不是合法 IPv4", "形如 192.168.2.188");
    if (!c.actuator.kmbox.uid.empty() && !ConfigSchema::IsHex8(c.actuator.kmbox.uid))
        return bad(1045, "actuator.kmbox.uid 必须是 8 位十六进制", "例如 529C5C53");

    // ---- 微调 ----
    if (c.aim.micro_adjust.step_px < 0 || c.aim.micro_adjust.step_px > 200)
        return bad(1046, "aim.micro_adjust.step_px 超出 [0,200]", "微调步进像素");

    // ---- 容器规模上限（防止畸形配置把内存/CPU 拖垮）----
    if (c.fov.ads_levels.size() > 32)
        return bad(1047, "fov.ads_levels 超过 32 档", "实际倍镜不会超过 32 档");
    if (c.inference.class_names.size() > 256 || c.inference.selected_class_names.size() > 256)
        return bad(1048, "类别表超过 256 项", "检查模型元数据是否被误写进配置");
    for (const auto& n : c.inference.class_names) {
        if (n.size() > 128) return bad(1049, "类别名超过 128 字符", "类别名应短小");
    }
    for (const auto& n : c.inference.selected_class_names) {
        if (n.size() > 128) return bad(1049, "类别名超过 128 字符", "类别名应短小");
    }

    // ---- 热键/按键名长度 ----
    {
        const std::string* keys[] = {&c.aim.hotkey_primary,   &c.aim.hotkey_secondary,
                                     &c.weapon.recoil.hotkey,  &c.weapon.trigger.key,
                                     &c.hotkeys.toggle_hud,    &c.hotkeys.toggle_team,
                                     &c.hotkeys.toggle_aim,    &c.hotkeys.adhesion_minus,
                                     &c.hotkeys.adhesion_plus, &c.hotkeys.push_switch,
                                     &c.hotkeys.target_up,     &c.hotkeys.target_down,
                                     &c.hotkeys.target_left,   &c.hotkeys.target_right};
        for (const std::string* k : keys) {
            if (k->size() > 64)
                return bad(1050, "按键名超过 64 字符", "按键名形如 VK_LBUTTON / F1");
        }
    }

    return Result<void>::Ok();
}

Result<Config> ConfigManager::LoadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Result<Config>::Fail(1005, "无法打开配置文件: " + path,
                                        "确认路径正确，缺失时将以默认值生成");
    // 大小上限：配置文件正常只有几十 KB。不设限时一个被替换成几个 GB 的文件
    // 会直接把进程内存吃光（而且解析器还要再分配一份等大的 JSON 树）。
    constexpr std::streamoff kMaxConfigBytes = 4 * 1024 * 1024;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size > kMaxConfigBytes) {
        return Result<Config>::Fail(1051, "配置文件过大: " + std::to_string(size) + " 字节",
                                    "正常配置应小于 4 MB，检查是否被替换");
    }
    f.seekg(0, std::ios::beg);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return FromJson(text);
}

Result<void> ConfigManager::SaveToFile(const std::string& path, const Config& cfg) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return Result<void>::Fail(1006, "无法写入配置文件: " + path,
                                      "检查目录权限与磁盘空间");
    f << ToJson(cfg, 2);
    if (!f) return Result<void>::Fail(1007, "写入配置时发生 IO 错误", "检查磁盘空间");
    return Result<void>::Ok();
}

} // namespace aim
