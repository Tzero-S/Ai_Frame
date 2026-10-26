#include "aim_test.h"
#include "core/frame_pipeline.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace aim;

namespace {

// 构造一帧：每个像素填 (b,g,r,a) = (seq&0xFF, x&0xFF, y&0xFF, 255)，便于校验内容
std::vector<uint8_t> MakeBgra(int w, int h, int seq) {
    std::vector<uint8_t> v(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            v[i + 0] = static_cast<uint8_t>(seq & 0xFF);
            v[i + 1] = static_cast<uint8_t>(x & 0xFF);
            v[i + 2] = static_cast<uint8_t>(y & 0xFF);
            v[i + 3] = 255;
        }
    }
    return v;
}

bool SubmitSeq(FrameDoubleBuffer& buf, int seq, int w = 32, int h = 16) {
    const auto px = MakeBgra(w, h, seq);
    return buf.Submit(px.data(), w, h, 1920, 1080, 10, 20, w, h, seq,
                      static_cast<uint64_t>(seq) * 1000);
}

} // namespace

// 基本交接：像素与元数据完整传递
AIM_TEST(frame_buffer_basic_handoff) {
    FrameDoubleBuffer buf(50);
    AIM_EXPECT_TRUE(SubmitSeq(buf, 7));

    OwnedFrame f;
    AIM_EXPECT_TRUE(buf.Acquire(f));
    AIM_EXPECT_TRUE(f.valid());
    AIM_EXPECT_EQ(f.w, 32);
    AIM_EXPECT_EQ(f.h, 16);
    AIM_EXPECT_EQ(f.seq, 7);
    AIM_EXPECT_EQ(static_cast<int>(f.ts_us), 7000);
    AIM_EXPECT_EQ(f.screen_w, 1920);
    AIM_EXPECT_EQ(f.src_x, 10);
    AIM_EXPECT_EQ(f.bytes(), static_cast<size_t>(32) * 16 * 4);
    // 抽查像素内容
    AIM_EXPECT_EQ(static_cast<int>(f.bgra[0]), 7);      // B = seq
    AIM_EXPECT_EQ(static_cast<int>(f.bgra[1]), 0);      // G = x
    AIM_EXPECT_EQ(static_cast<int>(f.bgra[2]), 0);      // R = y
    AIM_EXPECT_EQ(static_cast<int>(f.bgra[4 + 1]), 1);  // 第二像素 G = 1
}

// 无帧可取：超时返回 false，且不污染 out
AIM_TEST(frame_buffer_acquire_timeout_keeps_out) {
    FrameDoubleBuffer buf(20);
    OwnedFrame f;
    f.w = 123; // 预置哨兵
    AIM_EXPECT_FALSE(buf.Acquire(f));
    AIM_EXPECT_EQ(f.w, 123); // 未被修改
}

// 轮询模式（timeout=0）立即返回
AIM_TEST(frame_buffer_poll_mode) {
    FrameDoubleBuffer buf(0);
    OwnedFrame f;
    AIM_EXPECT_FALSE(buf.Acquire(f));
    AIM_EXPECT_TRUE(SubmitSeq(buf, 1));
    AIM_EXPECT_TRUE(buf.Acquire(f));
    AIM_EXPECT_EQ(f.seq, 1);
}

// 背压：连提 3 帧不取，只保留最后一帧，前两帧计入 dropped
AIM_TEST(frame_buffer_backpressure_keeps_latest) {
    FrameDoubleBuffer buf(20);
    AIM_EXPECT_TRUE(SubmitSeq(buf, 1));
    AIM_EXPECT_TRUE(SubmitSeq(buf, 2));
    AIM_EXPECT_TRUE(SubmitSeq(buf, 3));

    const auto st = buf.Stats();
    AIM_EXPECT_EQ(static_cast<int>(st.submitted), 3);
    AIM_EXPECT_EQ(static_cast<int>(st.dropped), 2);

    OwnedFrame f;
    AIM_EXPECT_TRUE(buf.Acquire(f));
    AIM_EXPECT_EQ(f.seq, 3); // 取到最新
    AIM_EXPECT_FALSE(buf.Acquire(f)); // 已取空
}

// 非法输入：全部被拒且不影响已有帧
AIM_TEST(frame_buffer_rejects_invalid) {
    FrameDoubleBuffer buf(20);
    const auto px = MakeBgra(8, 8, 0);

    AIM_EXPECT_FALSE(buf.Submit(nullptr, 8, 8, 1920, 1080, 0, 0, 8, 8, 0, 0));
    AIM_EXPECT_FALSE(buf.Submit(px.data(), 0, 8, 1920, 1080, 0, 0, 8, 8, 0, 0));
    AIM_EXPECT_FALSE(buf.Submit(px.data(), 8, 0, 1920, 1080, 0, 0, 8, 8, 0, 0));
    AIM_EXPECT_FALSE(buf.Submit(px.data(), -8, 8, 1920, 1080, 0, 0, 8, 8, 0, 0));
    // 超大：50000 x 50000 x 4 = 10 GB，超过单帧上限
    AIM_EXPECT_FALSE(buf.Submit(px.data(), 50000, 50000, 1920, 1080, 0, 0, 8, 8, 0, 0));

    const auto st = buf.Stats();
    AIM_EXPECT_EQ(static_cast<int>(st.rejected), 5);
    AIM_EXPECT_EQ(static_cast<int>(st.submitted), 0);

    OwnedFrame f;
    AIM_EXPECT_FALSE(buf.Acquire(f)); // 没有残留帧
}

// 尺寸变化：槽位能容纳不同尺寸，不串帧
AIM_TEST(frame_buffer_size_change) {
    FrameDoubleBuffer buf(20);
    AIM_EXPECT_TRUE(SubmitSeq(buf, 1, 16, 8));
    OwnedFrame f;
    AIM_EXPECT_TRUE(buf.Acquire(f));
    AIM_EXPECT_EQ(f.bytes(), static_cast<size_t>(16) * 8 * 4);

    AIM_EXPECT_TRUE(SubmitSeq(buf, 2, 64, 32));
    AIM_EXPECT_TRUE(buf.Acquire(f));
    AIM_EXPECT_EQ(f.w, 64);
    AIM_EXPECT_EQ(f.bytes(), static_cast<size_t>(64) * 32 * 4);
    AIM_EXPECT_EQ(f.seq, 2);
}

// 停止语义：Stop 后立即失败，未取走的帧计入 dropped；重复 Stop 安全
AIM_TEST(frame_buffer_stop_semantics) {
    FrameDoubleBuffer buf(200);
    AIM_EXPECT_TRUE(SubmitSeq(buf, 1));
    buf.Stop();
    AIM_EXPECT_TRUE(buf.stopped());

    OwnedFrame f;
    AIM_EXPECT_FALSE(buf.Acquire(f)); // 停止后不再交出残留帧

    const auto st = buf.Stats();
    AIM_EXPECT_EQ(static_cast<int>(st.submitted), 1);
    AIM_EXPECT_EQ(static_cast<int>(st.acquired), 0);
    AIM_EXPECT_EQ(static_cast<int>(st.dropped), 1);

    // 停止后提交被拒
    AIM_EXPECT_FALSE(SubmitSeq(buf, 2));
    AIM_EXPECT_EQ(static_cast<int>(buf.Stats().rejected), 1);

    buf.Stop(); // 幂等
    AIM_EXPECT_TRUE(buf.stopped());
}

// 停止能唤醒阻塞中的消费者（不等超时）
AIM_TEST(frame_buffer_stop_wakes_waiter) {
    FrameDoubleBuffer buf(5000); // 5s 超时
    std::atomic<bool> woke{false};
    std::thread t([&] {
        OwnedFrame f;
        const bool ok = buf.Acquire(f);
        woke = !ok; // 被 Stop 唤醒 → 返回 false
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto t0 = std::chrono::steady_clock::now();
    buf.Stop();
    t.join();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    AIM_EXPECT_TRUE(woke.load());
    AIM_EXPECT_TRUE(ms < 1000); // 远小于 5s 超时
}

// 计数守恒 + 并发正确性：单生产者 / 单消费者
AIM_TEST(frame_buffer_concurrent_counts) {
    constexpr int kFrames = 400;
    FrameDoubleBuffer buf(10);
    std::atomic<int> last_seq{-1};
    std::atomic<bool> monotonic{true};

    std::thread producer([&] {
        for (int i = 0; i < kFrames; ++i) {
            SubmitSeq(buf, i);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        buf.Stop();
    });

    std::thread consumer([&] {
        OwnedFrame f;
        while (buf.Acquire(f)) {
            // 取到的序号必须单调不减（只可能跳过，不可能回退）
            if (f.seq < last_seq.load()) monotonic = false;
            last_seq = f.seq;
            // 校验像素与 seq 一致（防止槽位串帧）
            if (static_cast<int>(f.bgra[0]) != (f.seq & 0xFF)) monotonic = false;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });

    producer.join();
    consumer.join();

    const auto st = buf.Stats();
    AIM_EXPECT_EQ(static_cast<int>(st.submitted), kFrames);
    AIM_EXPECT_EQ(static_cast<int>(st.acquired + st.dropped),
                  static_cast<int>(st.submitted)); // 停止后无残留
    AIM_EXPECT_TRUE(monotonic.load());
    AIM_EXPECT_TRUE(st.acquired > 0);
}

// 统计重置
AIM_TEST(frame_buffer_reset_stats) {
    FrameDoubleBuffer buf(10);
    AIM_EXPECT_TRUE(SubmitSeq(buf, 1));
    OwnedFrame f;
    AIM_EXPECT_TRUE(buf.Acquire(f));
    buf.ResetStats();
    const auto st = buf.Stats();
    AIM_EXPECT_EQ(static_cast<int>(st.submitted), 0);
    AIM_EXPECT_EQ(static_cast<int>(st.acquired), 0);
}

// TryAcquire：不等待、语义与 Acquire 一致、停止后失败
AIM_TEST(frame_buffer_try_acquire) {
    FrameDoubleBuffer buf(500); // 超时很长，用于证明 TryAcquire 不等待
    OwnedFrame f;
    AIM_EXPECT_FALSE(buf.TryAcquire(f)); // 无帧立即 false

    AIM_EXPECT_TRUE(SubmitSeq(buf, 9));
    AIM_EXPECT_TRUE(buf.TryAcquire(f));
    AIM_EXPECT_EQ(f.seq, 9);
    AIM_EXPECT_FALSE(buf.TryAcquire(f)); // 已取空

    AIM_EXPECT_TRUE(SubmitSeq(buf, 10));
    buf.Stop();
    AIM_EXPECT_FALSE(buf.TryAcquire(f)); // 停止后不再交出
}

// TryAcquire 的耗时必须有上界（不能退化成等待）
AIM_TEST(frame_buffer_try_acquire_is_nonblocking) {
    FrameDoubleBuffer buf(5000);
    OwnedFrame f;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) (void)buf.TryAcquire(f);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    AIM_EXPECT_TRUE(ms < 500); // 1000 次空转远小于 5s 超时
}

// 大帧 + 并发：960x960（3.7 MB）下内容不撕裂、计数守恒
AIM_TEST(frame_buffer_large_frame_integrity) {
    constexpr int kW = 960, kH = 960, kFrames = 60;
    FrameDoubleBuffer buf(10);
    std::atomic<bool> ok{true};

    std::thread producer([&] {
        std::vector<uint8_t> px(static_cast<size_t>(kW) * kH * 4);
        for (int i = 0; i < kFrames; ++i) {
            std::memset(px.data(), i & 0xFF, px.size());
            buf.Submit(px.data(), kW, kH, 1920, 1080, 0, 0, kW, kH, i,
                       static_cast<uint64_t>(i));
        }
        buf.Stop();
    });

    std::thread consumer([&] {
        OwnedFrame f;
        while (buf.Acquire(f)) {
            // 整帧必须是同一个填充值（撕裂会混入上一帧的内容）
            const uint8_t want = static_cast<uint8_t>(f.seq & 0xFF);
            if (f.bytes() != static_cast<size_t>(kW) * kH * 4) ok = false;
            if (f.bgra.empty() || f.bgra.front() != want || f.bgra.back() != want) ok = false;
        }
    });

    producer.join();
    consumer.join();
    const auto st = buf.Stats();
    AIM_EXPECT_TRUE(ok.load());
    AIM_EXPECT_EQ(static_cast<int>(st.submitted), kFrames);
    AIM_EXPECT_EQ(static_cast<int>(st.acquired + st.dropped), kFrames);
}

int main() {
    return aim::test::run_all();
}
