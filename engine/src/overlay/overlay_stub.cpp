#include "overlay/overlay_stub.h"
#include "overlay/overlay_spi.h"
#include "core/registry.h"
#include <memory>

namespace aim::overlay {

void NullOverlay::draw(const spi::Frame&, const std::vector<Detection>&) {}

void RegisterOverlayFactories() {
    // "none"：显式关闭覆盖层（调试/无窗场景）。
    Registry::register_factory<spi::IOverlay>("none", [] {
        return std::make_unique<NullOverlay>();
    });
    // "d3d11"：真实分层窗口覆盖层（2D DIB 路径，接口与 D3D11 方案一致）。
    Registry::register_factory<spi::IOverlay>("d3d11", [] {
        return std::make_unique<OverlayLayer>();
    });
}

} // namespace aim::overlay
