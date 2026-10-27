#include "aim_test.h"
#include "core/runtime.h"
#include "core/json.h"
#include "ipc/framing.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace aim;

AIM_TEST(runtime_start_stop_uptime) {
    EngineRuntime rt;
    rt.Start(1000);
    AIM_EXPECT_TRUE(rt.Running());
    AIM_EXPECT_EQ(static_cast<long long>(rt.Snapshot(1500).uptime_ms), 500LL);
    rt.Stop();
    AIM_EXPECT_FALSE(rt.Running());
    AIM_EXPECT_EQ(static_cast<long long>(rt.Snapshot(2000).uptime_ms), 0LL);
}

AIM_TEST(runtime_due_snapshot_1hz) {
    EngineRuntime rt;
    rt.Start(0);
    AIM_EXPECT_FALSE(rt.DueForSnapshot(500));
    AIM_EXPECT_TRUE(rt.DueForSnapshot(1000));
    AIM_EXPECT_FALSE(rt.DueForSnapshot(1500));
    AIM_EXPECT_TRUE(rt.DueForSnapshot(2000));
    rt.Stop();
    AIM_EXPECT_FALSE(rt.DueForSnapshot(9000)); // 停止后不再产生
}

AIM_TEST(runtime_onframe_counters) {
    EngineRuntime rt;
    rt.Start(0);

    PipelineResult r;
    r.ok = true;
    r.has_target = true;
    r.seq = 7;
    FrameTimings t;
    t.cap_ms = 1.0;
    t.infer_ms = 2.0;
    t.post_ms = 3.0;
    t.send_ms = 4.0;

    rt.OnFrame(100, r, t);
    rt.OnFrame(200, r, t);

    const StatusSnapshot s = rt.Snapshot(200);
    AIM_EXPECT_EQ(static_cast<long long>(s.frames), 2LL);
    AIM_EXPECT_NEAR(s.fps, 10.0, 0.01); // 100ms 间隔
    AIM_EXPECT_TRUE(s.has_target);
    AIM_EXPECT_EQ(s.seq, 7u);
    AIM_EXPECT_NEAR(s.timings.cap_ms, 1.0, 0.01);

    rt.OnDrop(3);
    AIM_EXPECT_EQ(rt.Snapshot(200).drop_count, 3);
}

AIM_TEST(runtime_snapshot_json) {
    EngineRuntime rt;
    rt.Start(0);
    rt.SetSegments(SegmentState::Running, SegmentState::Degraded, SegmentState::Failed);

    const std::string js = rt.SnapshotJson(1000);
    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(js, v, e));
    AIM_EXPECT_TRUE(v["running"].asBool());
    AIM_EXPECT_STREQ(v["segments"]["capture"].asString(), "running");
    AIM_EXPECT_STREQ(v["segments"]["inference"].asString(), "degraded");
    AIM_EXPECT_STREQ(v["segments"]["actuator"].asString(), "failed");
}

AIM_TEST(watchdog_limits) {
    Watchdog w(3, 60000);
    AIM_EXPECT_TRUE(w.AllowRestart(0));
    AIM_EXPECT_TRUE(w.AllowRestart(1000));
    AIM_EXPECT_TRUE(w.AllowRestart(2000));
    AIM_EXPECT_FALSE(w.AllowRestart(3000)); // 1 分钟内第 4 次
    AIM_EXPECT_EQ(w.RestartCount(3000), 3);
    AIM_EXPECT_TRUE(w.AllowRestart(61001)); // 窗口滑过后恢复
}

AIM_TEST(framing_roundtrip) {
    const std::string f = ipc::EncodeFrame("hello");
    AIM_EXPECT_EQ(f.size(), 9u);
    AIM_EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(f[0])), 5);

    std::string buf = f;
    std::string out;
    AIM_EXPECT_TRUE(ipc::TryDecodeFrame(buf, out));
    AIM_EXPECT_STREQ(out, "hello");
    AIM_EXPECT_TRUE(buf.empty());
}

AIM_TEST(framing_partial_and_multiple) {
    std::string buf = ipc::EncodeFrame("ab");
    buf += ipc::EncodeFrame("cde");

    std::string partial = buf.substr(0, 5);
    std::string out;
    AIM_EXPECT_FALSE(ipc::TryDecodeFrame(partial, out)); // 半包
    AIM_EXPECT_TRUE(ipc::TryDecodeFrame(buf, out));
    AIM_EXPECT_STREQ(out, "ab");
    AIM_EXPECT_TRUE(ipc::TryDecodeFrame(buf, out));
    AIM_EXPECT_STREQ(out, "cde");
    AIM_EXPECT_FALSE(ipc::TryDecodeFrame(buf, out));
}

AIM_TEST(framing_oversize_clears) {
    std::string buf(4, '\0');
    buf[0] = static_cast<char>(0xFF);
    buf[1] = static_cast<char>(0xFF);
    buf[2] = static_cast<char>(0xFF);
    buf[3] = static_cast<char>(0xFF);
    std::string out;
    AIM_EXPECT_FALSE(ipc::TryDecodeFrame(buf, out));
    AIM_EXPECT_TRUE(buf.empty());
}

// P0-1 回归：IPC 线程读快照与推理线程写状态并发时，必须无崩溃/无死锁。
// 没有互斥保护时，SnapshotJson 遍历 detections_ 与 SetDetections 的 assign 竞争，
// 会读到已释放的缓冲区（ASan 下报 heap-use-after-free）。
AIM_TEST(runtime_concurrent_snapshot_and_writes) {
    EngineRuntime rt;
    rt.Start(0);

    // 先写一帧再开线程：否则读线程的第一次快照必然是 frames=0，
    // 会被下面的"撕裂读"判据误判（该判据只在已有帧时才有意义）。
    {
        PipelineResult seed;
        seed.ok = true;
        seed.seq = 1;
        rt.OnFrame(1, seed, FrameTimings{});
    }

    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};

    std::thread writer([&] {
        std::vector<Detection> dets;
        for (int i = 0; i < 16; ++i) {
            Detection d;
            d.box = RectF{static_cast<float>(i), static_cast<float>(i), 10.f, 20.f};
            d.score = 0.5f;
            d.class_id = i % 3;
            dets.push_back(d);
        }
        int64_t now = 1;
        while (!stop.load(std::memory_order_relaxed)) {
            PipelineResult r;
            r.ok = true;
            r.has_target = true;
            r.seq = static_cast<uint32_t>(now);
            FrameTimings t;
            t.infer_ms = 1.0;
            rt.OnFrame(now++, r, t);
            // 交替提交不同长度，制造 vector 的扩容/释放
            if (now % 2 == 0) {
                rt.SetDetections(dets);
            } else {
                rt.SetDetections(std::vector<Detection>{});
            }
            rt.OnDrop(1);
        }
    });

    std::atomic<int> bad_json{0};
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const std::string js = rt.SnapshotJson(42);
            if (js.find("\"running\"") == std::string::npos ||
                js.find("\"detections\"") == std::string::npos) {
                bad_json.fetch_add(1);
            }
            const StatusSnapshot s = rt.Snapshot(42);
            if (s.seq == 0 && s.frames == 0) bad_json.fetch_add(1); // 撕裂读的粗筛
            reads.fetch_add(1);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);
    writer.join();
    reader.join();

    AIM_EXPECT_TRUE(reads.load() > 0);
    AIM_EXPECT_EQ(bad_json.load(), 0); // 每份快照都是自洽的 JSON
    // 收尾：最后写入的状态仍可读，且计数一致
    const StatusSnapshot fin = rt.Snapshot(1000);
    AIM_EXPECT_TRUE(fin.frames > 0);
    AIM_EXPECT_TRUE(fin.drop_count > 0);
}

int main() {
    return aim::test::run_all();
}
