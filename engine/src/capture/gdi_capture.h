#pragma once
#include <memory>
#include "core/spi.h"

// GDI 采集（§5.2.1）：BitBlt 从屏幕 DC 拷贝中心 ROI 到 32bpp DIB。
// 适用于窗口化/受限环境；延迟 2–8ms。
namespace aim::capture {

class GdiCapture final : public spi::ICapture {
public:
    GdiCapture();
    ~GdiCapture() override;
    GdiCapture(const GdiCapture&) = delete;
    GdiCapture& operator=(const GdiCapture&) = delete;

    bool init(const spi::CaptureSpec& spec) override;
    spi::Frame grab(const spi::CaptureSpec& spec) override;

    const char* status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aim::capture
