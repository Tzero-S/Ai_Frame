#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include "core/pipeline.h"

// 引擎运行时与心跳（§4.2 进程模型 / §8.1 status.snapshot 1Hz / §5.14 遥测）。
// 负责：运行状态、各段健康度、帧计数/FPS/耗时、周期快照、看门狗重启计数。
namespace aim {

enum class SegmentState { Running, Degraded, Failed };
const char* to_string(SegmentState s);

struct FrameTimings {
    double cap_ms = 0.0;
    double infer_ms = 0.0;
    double post_ms = 0.0;
    double send_ms = 0.0;
};

struct StatusSnapshot {
    bool running = false;
    int64_t uptime_ms = 0;
    double fps = 0.0;
    FrameTimings timings;
    uint64_t frames = 0;
    int drop_count = 0;
    SegmentState capture = SegmentState::Failed;
    SegmentState inference = SegmentState::Failed;
    SegmentState actuator = SegmentState::Failed;
    bool has_target = false;
    uint32_t seq = 0;
};

// 线程模型（重要）：写路径（OnFrame/OnDrop/SetDetections/SetSegments）由推理线程调用，
// 读路径（Snapshot/SnapshotJson）由 IPC 线程调用——两侧并发，内部一律加锁。
// 早期版本没有互斥：管道线程遍历 detections_ 时推理线程 assign() 会释放旧缓冲，
// 读侧持有悬垂指针（堆破坏）。改这里之前先想清楚调用方在哪个线程。
class EngineRuntime {
public:
    void Start(int64_t now_ms);
    void Stop();
    bool Running() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return running_;
    }

    void SetSegments(SegmentState capture, SegmentState inference, SegmentState actuator);
    void OnFrame(int64_t now_ms, const PipelineResult& r, const FrameTimings& t);
    void OnDrop(int n = 1) {
        std::lock_guard<std::mutex> lk(mtx_);
        drop_count_ += n;
    }

    // 最近一帧检测（供 status.snapshot 推送给控制台/覆盖层，最多保留 16 个）
    void SetDetections(const std::vector<Detection>& dets);

    StatusSnapshot Snapshot(int64_t now_ms) const;
    std::string SnapshotJson(int64_t now_ms) const;

    // 1Hz 触发：返回 true 表示本次应下发 status.snapshot（并推进计时）。
    bool DueForSnapshot(int64_t now_ms);

private:
    // 已持锁版本：供 Snapshot/SnapshotJson 复用，避免同一把非递归锁二次加锁
    StatusSnapshot SnapshotLocked(int64_t now_ms) const;

    mutable std::mutex mtx_;
    bool running_ = false;
    int64_t started_ms_ = 0;
    int64_t last_snapshot_ms_ = 0;
    int64_t last_frame_ms_ = -1;
    double frame_interval_ms_ = 0.0;
    uint64_t frames_ = 0;
    int drop_count_ = 0;
    double fps_ = 0.0;
    FrameTimings timings_{};
    SegmentState capture_ = SegmentState::Failed;
    SegmentState inference_ = SegmentState::Failed;
    SegmentState actuator_ = SegmentState::Failed;
    bool has_target_ = false;
    uint32_t seq_ = 0;
    std::vector<Detection> detections_;
};

// 看门狗（§4.2：引擎最多被自动拉起 3 次/分钟）。
class Watchdog {
public:
    explicit Watchdog(int max_restarts = 3, int64_t window_ms = 60000);

    // 记录一次重启尝试；窗口内超过上限返回 false。
    bool AllowRestart(int64_t now_ms);
    int RestartCount(int64_t now_ms) const;

private:
    void Prune(int64_t now_ms) const;

    int max_restarts_;
    int64_t window_ms_;
    mutable std::deque<int64_t> restarts_;
};

} // namespace aim
