#pragma once
#include <cstdint>
#include <string>
#include "overlay/primitive_batch.h"

// 覆盖层窗口（§5.11.3 / §9.1 overlay_window）：
// 透明置顶、点击穿透、工具窗口；以 32bpp DIB + UpdateLayeredWindow 呈现
// （文档允许 "D3D11/2D"，此处为 2D 路径；D3D11 渲染可后续替换 present 实现）。
namespace aim::overlay {

class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool init(int width, int height, bool click_through = true, std::string* err = nullptr);
    void shutdown();
    void set_visible(bool v);

    void present(const PrimitiveBatch& batch);

    void* handle() const { return hwnd_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int present_count() const { return present_count_; }
    int last_vertex_count() const { return last_vertex_count_; }
    const uint32_t* pixels() const { return pixels_; } // 测试/诊断用

private:
    void* hwnd_ = nullptr;
    void* mem_dc_ = nullptr;
    void* dib_ = nullptr;
    void* old_bmp_ = nullptr; // SelectObject 的返回值：shutdown 时还原，避免删被引用的位图
    uint32_t* pixels_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int present_count_ = 0;
    int last_vertex_count_ = 0;
};

} // namespace aim::overlay
