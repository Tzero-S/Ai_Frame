#pragma once
#include "core/spi.h"

// 覆盖层 SPI "none" 实现（§5.11.3）：显式关闭覆盖层（调试 / 无头测试）。
// 真实分层窗口实现见 overlay_spi.h（注册名 "d3d11"）；两者注册名对齐 §4.4。
namespace aim::overlay {

class NullOverlay final : public spi::IOverlay {
public:
    void draw(const spi::Frame& frame, const std::vector<Detection>& dets) override;
    const char* status() const { return "none: 覆盖层已关闭"; }
};

void RegisterOverlayFactories();

} // namespace aim::overlay
