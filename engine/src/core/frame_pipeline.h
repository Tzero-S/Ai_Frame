#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

// 采集 / 推理双缓冲通道（§4.3 数据面并行化）。
//
// 解决的问题：`ICapture::grab()` 返回的 `Frame.bgra` 是**借用**指针，指向采集对象
// 内部的复用缓冲（DXGI 在 ACCESS_LOST 时还会 `buffer.clear()`）。一旦采集与推理
// 分线程，消费方仍持有旧指针就会读到被覆盖的数据甚至悬垂内存。
//
// 本通道的做法：
//   1) 生产者提交时**拷贝**像素到自有槽位 —— 采集侧怎么复用/重建缓冲都不影响；
//   2) 两个槽位轮换，消费者取走后把向量 swap 出去（零拷贝交接）；
//   3) 背压策略 = 只保留最新一帧：消费者没跟上时，旧帧直接丢弃并计数；
//   4) 停止语义确定：Stop() 之后 Acquire 立即返回 false，不残留半帧。
//
// 线程模型：单生产者 + 单消费者。多消费者不在此组件职责内。
namespace aim {

// 一帧被接管的像素数据（自有内存，生命周期与采集缓冲无关）
struct OwnedFrame {
    std::vector<uint8_t> bgra;
    int w = 0;
    int h = 0;
    int screen_w = 0;
    int screen_h = 0;
    int src_x = 0;
    int src_y = 0;
    int src_w = 0;
    int src_h = 0;
    int seq = 0;
    uint64_t ts_us = 0;

    bool valid() const { return !bgra.empty() && w > 0 && h > 0; }
    size_t bytes() const { return bgra.size(); }
};

struct FramePipelineStats {
    uint64_t submitted = 0; // 成功入队的帧
    uint64_t acquired = 0;  // 被消费者取走的帧
    uint64_t dropped = 0;   // 因背压被丢弃的旧帧
    uint64_t rejected = 0;  // 非法输入被拒
};

class FrameDoubleBuffer {
public:
    static constexpr int kSlots = 2;
    static constexpr size_t kMaxFrameBytes = 64ull * 1024 * 1024; // 单帧上限（≈4096² BGRA）

    // acquire_timeout_ms：消费者单次等待上限（<=0 表示不等待，仅轮询）
    explicit FrameDoubleBuffer(int acquire_timeout_ms = 8);
    ~FrameDoubleBuffer();

    FrameDoubleBuffer(const FrameDoubleBuffer&) = delete;
    FrameDoubleBuffer& operator=(const FrameDoubleBuffer&) = delete;

    // 生产者：提交一帧。像素会被拷贝进自有槽位。
    // 返回 false 表示被拒（空指针 / 非正尺寸 / 超出单帧上限 / 已停止），见 Stats().rejected。
    bool Submit(const uint8_t* bgra, int w, int h,
                int screen_w, int screen_h,
                int src_x, int src_y, int src_w, int src_h,
                int seq, uint64_t ts_us);

    // 消费者：取最新一帧。超时或已停止返回 false（out 不被修改）。
    bool Acquire(OwnedFrame& out);

    // 消费者：非阻塞取帧（不等锁等待，只看当前是否有帧）。适合已有节拍驱动的循环。
    bool TryAcquire(OwnedFrame& out);

    // 停止：唤醒所有等待者，丢弃未取走的帧。可重复调用。
    void Stop();
    bool stopped() const;

    // 重置为可用状态（重启/复用场景）：清空待取帧、清除停止标志、更新超时。
    // 调用前必须确保没有生产者/消费者在跑。
    void Reset(int acquire_timeout_ms = 8);

    FramePipelineStats Stats() const;
    void ResetStats();

private:
    struct Slot {
        std::vector<uint8_t> data;
        int w = 0, h = 0;
        int screen_w = 0, screen_h = 0;
        int src_x = 0, src_y = 0, src_w = 0, src_h = 0;
        int seq = 0;
        uint64_t ts_us = 0;
    };

    mutable std::mutex m_;
    std::condition_variable cv_;
    Slot slots_[kSlots];
    // 生产者私有暂存区：像素拷贝在锁外完成，锁内只做 O(1) 的向量交换，
    // 避免"持锁 memcpy 大图"把消费者挡在门外（960² 一帧约 3.7 MB）。
    std::vector<uint8_t> scratch_;
    int ready_ = -1;       // 有未取走帧的槽位索引；-1 表示空
    int last_written_ = -1;
    bool stopped_ = false;
    int timeout_ms_ = 8;
    FramePipelineStats stats_{};
};

} // namespace aim
