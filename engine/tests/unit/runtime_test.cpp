#include "aim_test.h"
#include "core/runtime.h"
#include "core/json.h"
#include "ipc/framing.h"

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

int main() {
    return aim::test::run_all();
}
