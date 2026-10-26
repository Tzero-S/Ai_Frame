#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "core/frame_pipeline.h"
#include "core/spi.h"

// 采集泵（§4.3 数据面并行化）：把 ICapture::grab 搬到独立线程，
// 产出的帧写入 FrameDoubleBuffer，推理线程按需取最新帧。
//
// 生命周期契约（必须遵守，否则会踩悬垂）：
//   1) Start 之后才能取 buffer()；
//   2) 析构/关闭顺序必须是 Stop() → join → 再销毁 ICapture，
//      否则采集线程可能在 capture 已析构后继续调用 grab()。
//   3) Stop() 幂等，且会等待线程真正退出后才返回。
namespace aim {

class CapturePump {
public:
    struct Stats {
        uint64_t grabs = 0;    // 调用 grab 次数
        uint64_t empty = 0;    // grab 返回空帧（超时/重建中）
        uint64_t submits = 0;  // 成功入队
        uint64_t rejected = 0; // 提交被拒（尺寸非法/通道已停）
    };

    CapturePump() = default;
    ~CapturePump();

    CapturePump(const CapturePump&) = delete;
    CapturePump& operator=(const CapturePump&) = delete;

    // target_fps：采集节拍上限（<=0 表示不节流，跟采集源自身节奏）。
    // acquire_timeout_ms：消费者取帧等待上限。
    bool Start(spi::ICapture* capture, const spi::CaptureSpec& spec,
               int target_fps, int acquire_timeout_ms, std::string* err = nullptr);

    // 停止并等待线程退出；幂等。
    void Stop();
    bool running() const { return running_.load(); }

    // 仅在 Start 成功后有效；未启动时返回一个永远取不到帧的通道。
    FrameDoubleBuffer& buffer() { return buf_; }
    Stats stats() const;

private:
    void Loop();

    FrameDoubleBuffer buf_{8};
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    spi::ICapture* capture_ = nullptr;
    spi::CaptureSpec spec_{};
    int frame_period_ms_ = 0;
    std::atomic<uint64_t> grabs_{0};
    std::atomic<uint64_t> empty_{0};
    std::atomic<uint64_t> submits_{0};
    std::atomic<uint64_t> rejected_{0};
};

} // namespace aim
