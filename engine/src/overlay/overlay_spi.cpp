#include "overlay/overlay_spi.h"

#include <algorithm>

namespace aim::overlay {

OverlayLayer::~OverlayLayer() {
    if (impl_) impl_->shutdown();
}

int OverlayLayer::present_count() const {
    return impl_ ? impl_->present_count() : 0;
}

void OverlayLayer::set_visible(bool v) {
    if (impl_) impl_->set_visible(v);
}

void OverlayLayer::draw(const spi::Frame& frame, const std::vector<Detection>& dets) {
    if (!frame.valid || frame.w <= 0 || frame.h <= 0) return;

    // 覆盖层必须铺满"屏幕"而不是"ROI"：采集帧通常只是屏幕中心的 edge×edge 裁剪，
    // 直接按 frame.w/h 建窗会得到一个只覆盖 ROI 的小窗口（且定位在 0,0）。
    const int sw = (frame.screen_w > 0) ? frame.screen_w : frame.w;
    const int sh = (frame.screen_h > 0) ? frame.screen_h : frame.h;

    // 尺寸变化或首次绘制：重建分层透明窗口。
    if (!impl_ || sw != w_ || sh != h_) {
        if (impl_) {
            impl_->shutdown();
            impl_.reset();
        }
        auto preview = std::make_unique<OverlayPreview>();
        std::string err;
        if (!preview->init(sw, sh, /*click_through=*/true, &err)) {
            status_ = "init failed: " + err;
            return;
        }
        preview->set_visible(true);
        impl_ = std::move(preview);
        w_ = sw;
        h_ = sh;
        status_ = "ok";
    }

    // 与 main.cpp 预览口径一致：FOV 圈半径取短边 15%。
    const float fov_radius = static_cast<float>(std::min(sw, sh)) * 0.15f;
    // dets 已是屏幕坐标（Pipeline 用 ScaleRoiToScreen 映射），无需再偏移。
    impl_->render(dets, static_cast<float>(sw), static_cast<float>(sh), fov_radius);
}

} // namespace aim::overlay
