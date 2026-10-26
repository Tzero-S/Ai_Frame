#include "core/capture_pump.h"

#include <chrono>

namespace aim {

CapturePump::~CapturePump() { Stop(); }

bool CapturePump::Start(spi::ICapture* capture, const spi::CaptureSpec& spec,
                        int target_fps, int acquire_timeout_ms, std::string* err) {
    if (!capture) {
        if (err) *err = "CapturePump: capture 为空";
        return false;
    }
    if (running_.load()) {
        if (err) *err = "CapturePump: 已在运行";
        return false;
    }

    capture_ = capture;
    spec_ = spec;
    frame_period_ms_ = (target_fps > 0) ? (1000 / (target_fps > 1000 ? 1000 : target_fps)) : 0;
    buf_.Reset(acquire_timeout_ms > 0 ? acquire_timeout_ms : 8);
    stop_.store(false);

    running_.store(true);
    thread_ = std::thread([this] { Loop(); });
    return true;
}

void CapturePump::Stop() {
    if (!running_.load()) {
        // 仍可能有一次未 join 的线程（极端竞态），保守处理
        if (thread_.joinable()) thread_.join();
        return;
    }
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    running_.store(false);
    buf_.Stop(); // 唤醒可能仍在等待的消费者
}

void CapturePump::Loop() {
    using Clock = std::chrono::steady_clock;
    while (!stop_.load(std::memory_order_relaxed)) {
        const auto t0 = Clock::now();

        const spi::Frame f = capture_->grab(spec_);
        ++grabs_;
        if (f.valid && f.bgra && f.w > 0 && f.h > 0) {
            const bool ok = buf_.Submit(f.bgra, f.w, f.h, f.screen_w, f.screen_h, f.src_x,
                                        f.src_y, f.src_w, f.src_h, f.seq, f.ts);
            if (ok) {
                ++submits_;
            } else {
                ++rejected_;
            }
        } else {
            ++empty_;
        }

        // 节流策略（重要）：Windows 的 sleep 精度受系统定时器粒度限制，
        // 实测本机 sleep_for(5ms) 恒为 15.6ms，且 timeBeginPeriod(1) 无效。
        // 因此短周期（<16ms）绝不能用 sleep 控速——那样反而把采集限到 ~32fps。
        const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                               Clock::now() - t0)
                               .count();
        if (frame_period_ms_ >= 16) {
            // 周期够长，sleep 精度可接受；分片睡以便 Stop() 及时响应
            auto left = frame_period_ms_ - spent;
            while (left > 0 && !stop_.load(std::memory_order_relaxed)) {
                const auto step = (left > 5) ? 5 : left;
                std::this_thread::sleep_for(std::chrono::milliseconds(step));
                left -= step;
            }
        } else {
            // 短周期：靠采集源自身阻塞来控速（DXGI 会阻塞到下一帧）。
            // 若采集源不阻塞（GDI），这里只让出时间片，不会把帧率限死。
            std::this_thread::yield();
        }
    }
    buf_.Stop();
}

CapturePump::Stats CapturePump::stats() const {
    Stats s;
    s.grabs = grabs_.load();
    s.empty = empty_.load();
    s.submits = submits_.load();
    s.rejected = rejected_.load();
    return s;
}

} // namespace aim
