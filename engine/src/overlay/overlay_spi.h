#pragma once
#include <memory>
#include <string>
#include <vector>
#include "core/spi.h"
#include "overlay/overlay_preview.h"

// 覆盖层 SPI 真实实现（§4.4 / §5.11.3）：注册名 "d3d11"。
// 文档允许 D3D11/D2D 渲染；当前 present 为 32bpp DIB + UpdateLayeredWindow 的
// 2D 路径（见 overlay_window.h），对外接口与 D3D11 方案一致，后续可替换 present。
//
// 懒初始化：首次 draw 时按帧尺寸创建分层透明窗口（点击穿透 + 工具窗口）；
// 尺寸变化时重建。窗口创建失败不抛异常，仅记录状态并静默降级。
namespace aim::overlay {

class OverlayLayer final : public spi::IOverlay {
public:
    OverlayLayer() = default;
    ~OverlayLayer() override;

    OverlayLayer(const OverlayLayer&) = delete;
    OverlayLayer& operator=(const OverlayLayer&) = delete;

    void draw(const spi::Frame& frame, const std::vector<Detection>& dets) override;
    void set_visible(bool v) override;

    // 诊断/状态（供 UI 或测试查询）；内容为 "ok" 或失败原因。
    const char* status() const { return status_.c_str(); }
    bool ready() const { return impl_ != nullptr; }
    int present_count() const;
    // 当前覆盖层窗口尺寸（未创建时为 0）
    int width() const { return w_; }
    int height() const { return h_; }

private:
    std::unique_ptr<OverlayPreview> impl_;
    std::string status_ = "uninitialized";
    int w_ = 0;
    int h_ = 0;
};

} // namespace aim::overlay
