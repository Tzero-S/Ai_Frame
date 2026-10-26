#pragma once
#include <string>
#include "core/config.h"
#include "core/pipeline.h"
#include "core/spi.h"

// 配置 → 运行时装配（§5.1.3「单一真源」的落地）：
// 把 Config 翻译成 PipelineSettings、采集规格与各 SPI 注册名，
// 避免运行入口里散落手工构造（此前 --preview 即为此类）。
namespace aim {

struct BuiltSettings {
    PipelineSettings pipeline;
    spi::CaptureSpec capture_spec;

    std::string capture_name;   // dxgi | gdi | scrcpy（§4.4）
    std::string backend_name;   // dml | cpu | tensorrt
    std::string actuator_name;  // sendinput | ghub | kmbox | serial

    std::string game_title;     // 焦点判定用（capture.game_window.title_pattern）
    int target_fps = 120;       // 循环节流上限
    bool zero_tensors = true;   // M9 L2：推理输出缓冲用后清零
    bool neutral_thread_names = true; // M9 L2：线程名中性化
};

// screen_w/h：主显示器分辨率（由 fov/resolution_detect 提供）。
// 纯函数：不读文件、不碰硬件，便于单测。
BuiltSettings BuildSettings(const Config& cfg, int screen_w, int screen_h);

} // namespace aim
