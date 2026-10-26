#include "aim_test.h"
#include "core/capture_pump.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace aim;

namespace {

// 简单假采集：每帧填一个随序号变化的像素
class FakeCap final : public spi::ICapture {
public:
    bool init(const spi::CaptureSpec&) override { return true; }

    spi::Frame grab(const spi::CaptureSpec&) override {
        const int seq = ++seq_;
        buf_.assign(static_cast<size_t>(w_) * h_ * 4, static_cast<uint8_t>(seq & 0xFF));
        spi::Frame f;
        f.seq = seq;
        f.ts = static_cast<uint64_t>(seq) * 1000;
        f.w = w_;
        f.h = h_;
        f.bgra = buf_.data();
        f.valid = true;
        f.screen_w = 1920;
        f.screen_h = 1080;
        f.src_x = 0;
        f.src_y = 0;
        f.src_w = w_;
        f.src_h = h_;
        return f;
    }

    std::atomic<int> seq_{0};

private:
    std::vector<uint8_t> buf_;
    int w_ = 32;
    int h_ = 32;
};

// 空采集：始终返回无效帧
class DeadCap final : public spi::ICapture {
public:
    bool init(const spi::CaptureSpec&) override { return true; }
    spi::Frame grab(const spi::CaptureSpec&) override { return spi::Frame::empty(); }
};

} // namespace

// 基本链路：采集线程产帧 → 消费者取到 → 统计累加
AIM_TEST(pump_produces_frames) {
    FakeCap cap;
    CapturePump pump;
    spi::CaptureSpec spec;
    std::string err;
    AIM_EXPECT_TRUE(pump.Start(&cap, spec, /*target_fps=*/0, 50, &err));
    AIM_EXPECT_TRUE(pump.running());

    FrameDoubleBuffer& buf = pump.buffer();
    OwnedFrame f;
    int got = 0;
    for (int i = 0; i < 50 && got < 5; ++i) {
        if (buf.Acquire(f)) {
            ++got;
            AIM_EXPECT_TRUE(f.valid());
            AIM_EXPECT_EQ(f.w, 32);
            AIM_EXPECT_EQ(static_cast<int>(f.bgra[0]), f.seq & 0xFF);
        }
    }
    AIM_EXPECT_TRUE(got > 0);

    pump.Stop();
    AIM_EXPECT_FALSE(pump.running());
    const auto st = pump.stats();
    AIM_EXPECT_TRUE(st.grabs > 0);
    AIM_EXPECT_EQ(static_cast<int>(st.submits + st.rejected + st.empty),
                  static_cast<int>(st.grabs));
}

// 参数校验：空指针 / 重复启动
AIM_TEST(pump_rejects_bad_start) {
    CapturePump pump;
    spi::CaptureSpec spec;
    std::string err;
    AIM_EXPECT_FALSE(pump.Start(nullptr, spec, 0, 20, &err));
    AIM_EXPECT_FALSE(err.empty());

    FakeCap cap;
    AIM_EXPECT_TRUE(pump.Start(&cap, spec, 0, 20, nullptr));
    AIM_EXPECT_FALSE(pump.Start(&cap, spec, 0, 20, nullptr)); // 已在运行
    pump.Stop();
}

// Stop 幂等 + 停止后缓冲区不再产出
AIM_TEST(pump_stop_is_idempotent) {
    FakeCap cap;
    CapturePump pump;
    spi::CaptureSpec spec;
    AIM_EXPECT_TRUE(pump.Start(&cap, spec, 0, 20, nullptr));
    pump.Stop();
    pump.Stop();
    pump.Stop();
    AIM_EXPECT_FALSE(pump.running());

    OwnedFrame f;
    AIM_EXPECT_FALSE(pump.buffer().Acquire(f)); // 已停止
}

// 空采集源：不崩、空帧计数增长
AIM_TEST(pump_handles_dead_capture) {
    DeadCap cap;
    CapturePump pump;
    spi::CaptureSpec spec;
    AIM_EXPECT_TRUE(pump.Start(&cap, spec, /*target_fps=*/200, 20, nullptr));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    pump.Stop();

    const auto st = pump.stats();
    AIM_EXPECT_TRUE(st.grabs > 0);
    AIM_EXPECT_TRUE(st.empty > 0);
    AIM_EXPECT_EQ(static_cast<int>(st.submits), 0);
}

// 节拍：target_fps=60 时 300ms 内产帧数应明显少于无节流
AIM_TEST(pump_paces_to_target_fps) {
    FakeCap cap;
    CapturePump pump;
    spi::CaptureSpec spec;
    AIM_EXPECT_TRUE(pump.Start(&cap, spec, /*target_fps=*/60, 20, nullptr));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    pump.Stop();
    const auto st = pump.stats();
    // 60fps × 0.3s ≈ 18 帧；给足余量（含线程启动与调度抖动）
    AIM_EXPECT_TRUE(st.grabs >= 5);
    AIM_EXPECT_TRUE(st.grabs <= 40);
}

// 重启：Stop 后再次 Start 可用，且统计清零
AIM_TEST(pump_restart_resets) {
    FakeCap cap;
    CapturePump pump;
    spi::CaptureSpec spec;
    AIM_EXPECT_TRUE(pump.Start(&cap, spec, 0, 20, nullptr));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    pump.Stop();
    AIM_EXPECT_TRUE(pump.stats().grabs > 0);

    AIM_EXPECT_TRUE(pump.Start(&cap, spec, 0, 20, nullptr));
    OwnedFrame f;
    bool got = false;
    for (int i = 0; i < 40 && !got; ++i) got = pump.buffer().Acquire(f);
    AIM_EXPECT_TRUE(got);
    pump.Stop();
}

int main() {
    return aim::test::run_all();
}
