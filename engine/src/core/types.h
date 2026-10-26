#pragma once
#include <cstdint>
#include <vector>

// 共享基础类型（§4.3 / §5.4）。所有几何量以"屏幕像素"或"角度(度)"为单位，
// 字段名与文档保持一致（cx/cy、class_id、score、kpts）。
namespace aim {

struct PointF {
    float x = 0.f;
    float y = 0.f;
};

struct Vec2F {
    float x = 0.f;
    float y = 0.f;
};

struct SizeF {
    float w = 0.f;
    float h = 0.f;
};

struct RectF {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;

    float cx() const noexcept { return x + w * 0.5f; }
    float cy() const noexcept { return y + h * 0.5f; }
    float right() const noexcept { return x + w; }
    float bottom() const noexcept { return y + h; }
};

struct Keypoint {
    float x = 0.f;
    float y = 0.f;
    float score = 0.f;
};

// 单帧后处理输出（§5.4）。box 处于"模型输入坐标空间"(0..imgsz)，
// 由调用方按 §5.4 坐标映射折算回屏幕坐标。
struct Detection {
    RectF box;
    float score = 0.f;
    int class_id = 0;
    std::vector<Keypoint> kpts;

    PointF aim_point() const noexcept { return { box.cx(), box.cy() }; }
};

} // namespace aim
