#include "core/frame_pipeline.h"

#include <chrono>
#include <cstring>
#include <limits>

namespace aim {

namespace {

// 尺寸合法性：正数、乘积不溢出、不超过单帧上限
bool ValidShape(int w, int h, size_t* need_bytes) {
    if (w <= 0 || h <= 0) return false;
    const int64_t need = static_cast<int64_t>(w) * static_cast<int64_t>(h) * 4;
    if (need <= 0) return false;
    if (static_cast<uint64_t>(need) > FrameDoubleBuffer::kMaxFrameBytes) return false;
    if (need_bytes) *need_bytes = static_cast<size_t>(need);
    return true;
}

int ClampNonNeg(int v) { return (v < 0) ? 0 : v; }

} // namespace

FrameDoubleBuffer::FrameDoubleBuffer(int acquire_timeout_ms)
    : timeout_ms_(acquire_timeout_ms < 0 ? 0 : acquire_timeout_ms) {}

FrameDoubleBuffer::~FrameDoubleBuffer() { Stop(); }

bool FrameDoubleBuffer::Submit(const uint8_t* bgra, int w, int h,
                               int screen_w, int screen_h,
                               int src_x, int src_y, int src_w, int src_h,
                               int seq, uint64_t ts_us) {
    size_t need = 0;
    if (!bgra || !ValidShape(w, h, &need)) {
        std::lock_guard<std::mutex> lk(m_);
        ++stats_.rejected;
        return false;
    }

    // 像素拷贝放在锁外：scratch_ 是生产者私有，消费者碰不到。
    // 这样临界区只剩元数据赋值 + O(1) 向量交换，不会因为一帧 3.7 MB 把消费者挡住。
    scratch_.resize(need);
    std::memcpy(scratch_.data(), bgra, need);

    std::unique_lock<std::mutex> lk(m_);
    if (stopped_) {
        ++stats_.rejected;
        return false;
    }

    // 选槽：若上一帧还没被取走（消费者没跟上），直接覆盖它 = 丢弃旧帧（背压）
    int idx;
    if (ready_ >= 0) {
        idx = ready_;
        ++stats_.dropped;
    } else {
        idx = (last_written_ + 1) % kSlots;
    }

    Slot& s = slots_[idx];
    s.data.swap(scratch_); // 槽位拿到新像素，scratch_ 拿到旧缓冲继续复用
    s.w = w;
    s.h = h;
    s.screen_w = ClampNonNeg(screen_w);
    s.screen_h = ClampNonNeg(screen_h);
    // src_x/src_y **不夹取**：多显示器时虚拟桌面原点可以是负数（副屏在主屏左侧/上方），
    // 夹成 0 会让 ROI 反投影整体平移，检测框落在错误位置。只有宽高需要非负。
    s.src_x = src_x;
    s.src_y = src_y;
    s.src_w = ClampNonNeg(src_w);
    s.src_h = ClampNonNeg(src_h);
    s.seq = seq;
    s.ts_us = ts_us;

    ready_ = idx;
    last_written_ = idx;
    ++stats_.submitted;

    lk.unlock(); // 先解锁再通知，避免被唤醒者立刻阻塞在这把锁上
    cv_.notify_one();
    return true;
}

bool FrameDoubleBuffer::Acquire(OwnedFrame& out) {
    std::unique_lock<std::mutex> lk(m_);

    if (stopped_) return false;
    if (ready_ < 0) {
        if (timeout_ms_ <= 0) return false; // 轮询模式：不等待
        const bool got = cv_.wait_for(
            lk, std::chrono::milliseconds(timeout_ms_),
            [this] { return ready_ >= 0 || stopped_; });
        if (!got || stopped_) return false;
    }
    if (ready_ < 0) return false;

    const int idx = ready_;
    ready_ = -1;
    Slot& s = slots_[idx];

    // 零拷贝交接：把槽位里的向量换给消费者，槽位拿到消费者上一帧的缓冲复用
    out.bgra.swap(s.data);
    out.w = s.w;
    out.h = s.h;
    out.screen_w = s.screen_w;
    out.screen_h = s.screen_h;
    out.src_x = s.src_x;
    out.src_y = s.src_y;
    out.src_w = s.src_w;
    out.src_h = s.src_h;
    out.seq = s.seq;
    out.ts_us = s.ts_us;
    ++stats_.acquired;

    lk.unlock();
    cv_.notify_all(); // 槽位已空，通知生产者可以继续写
    return true;
}

bool FrameDoubleBuffer::TryAcquire(OwnedFrame& out) {
    std::unique_lock<std::mutex> lk(m_);
    if (stopped_ || ready_ < 0) return false;

    const int idx = ready_;
    ready_ = -1;
    Slot& s = slots_[idx];

    out.bgra.swap(s.data);
    out.w = s.w;
    out.h = s.h;
    out.screen_w = s.screen_w;
    out.screen_h = s.screen_h;
    out.src_x = s.src_x;
    out.src_y = s.src_y;
    out.src_w = s.src_w;
    out.src_h = s.src_h;
    out.seq = s.seq;
    out.ts_us = s.ts_us;
    ++stats_.acquired;

    lk.unlock();
    cv_.notify_all();
    return true;
}

void FrameDoubleBuffer::Reset(int acquire_timeout_ms) {
    std::lock_guard<std::mutex> lk(m_);
    stopped_ = false;
    ready_ = -1;
    last_written_ = -1;
    timeout_ms_ = (acquire_timeout_ms < 0) ? 0 : acquire_timeout_ms;
    stats_ = FramePipelineStats{};
    for (auto& s : slots_) {
        s.data.clear();
        s.w = s.h = 0;
    }
}

void FrameDoubleBuffer::Stop() {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (stopped_) return;
        stopped_ = true;
        if (ready_ >= 0) {
            ++stats_.dropped; // 未取走的帧计入丢弃，保证 submitted = acquired + dropped
        }
        ready_ = -1; // 丢弃未取走的帧，保证停止语义确定
    }
    cv_.notify_all();
}

bool FrameDoubleBuffer::stopped() const {
    std::lock_guard<std::mutex> lk(m_);
    return stopped_;
}

FramePipelineStats FrameDoubleBuffer::Stats() const {
    std::lock_guard<std::mutex> lk(m_);
    return stats_;
}

void FrameDoubleBuffer::ResetStats() {
    std::lock_guard<std::mutex> lk(m_);
    stats_ = FramePipelineStats{};
}

} // namespace aim
