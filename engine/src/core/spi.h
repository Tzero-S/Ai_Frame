#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/config.h"
#include "core/types.h"

// 扩展点 SPI（§4.4）：采集 / 推理 / 执行 / 覆盖层 / 热键。
// 所有实现以工厂注册（core/registry.h），业务侧零分支。
namespace aim::spi {

// ---- 采集 ----
struct CaptureSpec {
    int output_monitor = 0;
    int roi_w = 640;
    int roi_h = 640;
    // true = 采集整块输出（roi.mode=full）。采集器不能再把 roi_w 当正方形边长，
    // 否则 full 模式会退化成"居中正方形"：1920x1080 上只截到 1080x1080。
    bool full_frame = false;
};

// 一帧 BGRA8 视图（非拥有；生命周期由采集器保证到下次 grab）。
//
// ROI 语义（§5.2.2）：采集器通常只返回屏幕中心的 edge×edge 裁剪，
// 因此 w/h 是"ROI 尺寸"而非屏幕尺寸。src_* 描述该帧在屏幕中的位置，
// 供坐标映射（ScaleRoiToScreen）与覆盖层定位使用：
//   src_w/src_h == 0  → 未提供 ROI 信息，按"帧即整屏"处理（兼容旧实现/测试假件）
//   screen_w/screen_h == 0 → 未知屏幕尺寸，由调用方以自身配置为准
struct Frame {
    int seq = 0;
    uint64_t ts = 0;
    int w = 0;
    int h = 0;
    const uint8_t* bgra = nullptr;
    bool valid = false;

    // 采集源屏幕尺寸（像素）
    int screen_w = 0;
    int screen_h = 0;
    // 本帧在屏幕中的矩形（ROI 原点 + 尺寸）
    int src_x = 0;
    int src_y = 0;
    int src_w = 0;
    int src_h = 0;

    static Frame empty() { return Frame{}; }
};

// ---- 推理 ----
struct ModelSpec {
    std::string path;
    int imgsz = 640;
    std::string backend; // dml | cpu | tensorrt
    // 解码参数（§5.3.1 / §7.3）
    int num_classes = 1;
    int num_kpts = 0;
    float conf_threshold = 0.35f;
    // true：BGRA u8 NHWC [1,H,W,4]（图改写后的生产路径，§5.3.2）
    // false：float NCHW [1,3,H,W]（通用/测试路径）
    bool input_nhwc_u8 = true;
    int threads = 0; // 0 = ORT 自动
    // M9 L2（§5.12.2）：输出张量用后清零，避免检测结果残留在内存
    bool zero_outputs = false;
};

struct PerfCounters {
    float pre_ms = 0.f;
    float infer_ms = 0.f;
    float post_ms = 0.f;
};

// ---- 执行（§5.6.1 统一指令模型）----
struct MoveCmd {
    float dx_px = 0.f, dy_px = 0.f; // 期望屏幕像素位移（已含全部补偿）
    int duration_ms = 0;            // 期望到达时间
    bool bezier = false;            // 是否走贝塞尔路径
    uint32_t seq = 0;               // 序号，用于丢弃过期指令
};

struct ClickCmd {
    bool down = false;
    uint64_t hold_ms = 0;
};

// ---- 热键 ----
struct HotkeyEvent {
    int key = 0;
    bool down = false;
};

// ---- 接口 ----
class ICapture {
public:
    virtual ~ICapture() = default;
    // 设备级配置（scrcpy 的 serial/host/port 等）。默认无操作；返回 false 表示配置非法。
    virtual bool configure(const CaptureConfig&) { return true; }
    virtual bool init(const CaptureSpec& spec) = 0;
    virtual Frame grab(const CaptureSpec& spec) = 0;
};

class IBackend {
public:
    virtual ~IBackend() = default;
    virtual bool load(const ModelSpec& spec) = 0;
    virtual bool infer(const Frame& frame, std::vector<Detection>& out) = 0;
    virtual std::string backend_name() const = 0;
    virtual PerfCounters counters() const = 0;
};

class IActuator {
public:
    virtual ~IActuator() = default;
    // 通道级配置（KMBox 的 ip/port/uid、串口、GHUB 设备路径等）。
    // 默认无操作；返回 false 表示设备打开失败（调用方应中止运行）。
    virtual bool configure(const ActuatorConfig&) { return true; }
    virtual void move(const MoveCmd& cmd) = 0;
    virtual void click(const ClickCmd& cmd) = 0;
};

class IOverlay {
public:
    virtual ~IOverlay() = default;
    // 绘制一帧检测结果。frame 携带 ROI 与屏幕尺寸信息（见 Frame 注释），
    // dets 已由 Pipeline 映射为屏幕坐标。
    virtual void draw(const Frame& frame, const std::vector<Detection>& dets) = 0;
    // 显示/隐藏（可选能力）：默认空实现，无窗实现可直接忽略。
    virtual void set_visible(bool) {}
};

class IHotkeyBinding {
public:
    virtual ~IHotkeyBinding() = default;
    virtual bool poll(const HotkeyEvent& ev) = 0;
};

} // namespace aim::spi
