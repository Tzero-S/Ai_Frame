#pragma once
#include <memory>
#include "core/spi.h"

// DXGI 桌面复制采集（§5.2.2）：D3D11 设备 + IDXGIOutputDuplication，
// GPU 侧 CopySubresourceRegion 只回读中心 ROI，ACCESS_LOST 时重建 duplication。
namespace aim::capture {

class DxgiCapture final : public spi::ICapture {
public:
    DxgiCapture();
    ~DxgiCapture() override;
    DxgiCapture(const DxgiCapture&) = delete;
    DxgiCapture& operator=(const DxgiCapture&) = delete;

    bool init(const spi::CaptureSpec& spec) override;
    spi::Frame grab(const spi::CaptureSpec& spec) override;

    const char* status() const;
    int timeout_ms = 16; // AcquireNextFrame 超时

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aim::capture
