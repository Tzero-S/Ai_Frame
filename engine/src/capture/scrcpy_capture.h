#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/spi.h"

// scrcpy 投屏采集（§5.2.3）。
// 当前实现为文档中的"降级路径"：`adb -s SERIAL exec-out screencap -p` 取 PNG
// （约 10–20 FPS），再解码为 BGRA。H.264 直解（scrcpy server + 硬解）待解码链接入。
namespace aim::capture {

class ScrcpyCapture final : public spi::ICapture {
public:
    ScrcpyCapture();
    ~ScrcpyCapture() override;

    // 由配置层注入 scrcpy 参数（CaptureSpec 不含串口信息）
    void configure(const std::string& serial, const std::string& host, int port);
    // §4.4 SPI：从配置取 scrcpy 节（serial/host/port）
    bool configure(const CaptureConfig& cfg) override;

    bool init(const spi::CaptureSpec& spec) override;
    spi::Frame grab(const spi::CaptureSpec& spec) override;

    const char* status() const;

private:
    std::string serial_;
    std::string host_ = "127.0.0.1";
    int port_ = 27183;
    int edge_ = 640;
    uint32_t seq_ = 0;
    bool ready_ = false;
    std::vector<uint8_t> buffer_;
};

} // namespace aim::capture
