#include "core/runtime.h"
#include "core/json.h"
#include <algorithm>

namespace aim {

const char* to_string(SegmentState s) {
    switch (s) {
        case SegmentState::Running:  return "running";
        case SegmentState::Degraded: return "degraded";
        case SegmentState::Failed:   return "failed";
    }
    return "failed";
}

void EngineRuntime::Start(int64_t now_ms) {
    std::lock_guard<std::mutex> lk(mtx_);
    running_ = true;
    started_ms_ = now_ms;
    last_snapshot_ms_ = now_ms;
    last_frame_ms_ = -1;
    frame_interval_ms_ = 0.0;
    frames_ = 0;
    drop_count_ = 0;
    fps_ = 0.0;
    timings_ = FrameTimings{};
    has_target_ = false;
    seq_ = 0;
    detections_.clear();
}

void EngineRuntime::Stop() {
    std::lock_guard<std::mutex> lk(mtx_);
    running_ = false;
}

void EngineRuntime::SetDetections(const std::vector<Detection>& dets) {
    const size_t n = std::min<size_t>(dets.size(), 16);
    std::lock_guard<std::mutex> lk(mtx_);
    detections_.assign(dets.begin(), dets.begin() + static_cast<std::ptrdiff_t>(n));
}

void EngineRuntime::SetSegments(SegmentState capture, SegmentState inference, SegmentState actuator) {
    std::lock_guard<std::mutex> lk(mtx_);
    capture_ = capture;
    inference_ = inference;
    actuator_ = actuator;
}

void EngineRuntime::OnFrame(int64_t now_ms, const PipelineResult& r, const FrameTimings& t) {
    std::lock_guard<std::mutex> lk(mtx_);
    ++frames_;

    if (last_frame_ms_ >= 0) {
        const double dt = static_cast<double>(now_ms - last_frame_ms_);
        if (dt > 0.0) {
            frame_interval_ms_ = (frame_interval_ms_ <= 0.0)
                                     ? dt
                                     : frame_interval_ms_ + 0.1 * (dt - frame_interval_ms_);
            fps_ = 1000.0 / frame_interval_ms_;
        }
    }
    last_frame_ms_ = now_ms;

    auto ema = [](double& dst, double v) {
        dst = (dst <= 0.0) ? v : dst + 0.1 * (v - dst);
    };
    ema(timings_.cap_ms, t.cap_ms);
    ema(timings_.infer_ms, t.infer_ms);
    ema(timings_.post_ms, t.post_ms);
    ema(timings_.send_ms, t.send_ms);

    has_target_ = r.has_target;
    seq_ = r.seq;
}

StatusSnapshot EngineRuntime::Snapshot(int64_t now_ms) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return SnapshotLocked(now_ms);
}

// 调用方必须已持有 mtx_（SnapshotJson 一次加锁内完成取数与序列化）
StatusSnapshot EngineRuntime::SnapshotLocked(int64_t now_ms) const {
    StatusSnapshot s;
    s.running = running_;
    s.uptime_ms = running_ ? (now_ms - started_ms_) : 0;
    s.fps = fps_;
    s.timings = timings_;
    s.frames = frames_;
    s.drop_count = drop_count_;
    s.capture = capture_;
    s.inference = inference_;
    s.actuator = actuator_;
    s.has_target = has_target_;
    s.seq = seq_;
    return s;
}

std::string EngineRuntime::SnapshotJson(int64_t now_ms) const {
    std::lock_guard<std::mutex> lk(mtx_);
    const StatusSnapshot s = SnapshotLocked(now_ms);

    json::Value v;
    v["running"] = s.running;
    v["uptime_ms"] = static_cast<double>(s.uptime_ms);
    v["fps"] = s.fps;

    auto& t = v["timings"];
    t["cap_ms"] = s.timings.cap_ms;
    t["infer_ms"] = s.timings.infer_ms;
    t["post_ms"] = s.timings.post_ms;
    t["send_ms"] = s.timings.send_ms;

    v["frames"] = static_cast<double>(s.frames);
    v["drop_count"] = s.drop_count;

    auto& seg = v["segments"];
    seg["capture"] = to_string(s.capture);
    seg["inference"] = to_string(s.inference);
    seg["actuator"] = to_string(s.actuator);

    v["has_target"] = s.has_target;
    v["seq"] = static_cast<double>(s.seq);

    json::Value dets;
    for (const auto& d : detections_) {
        json::Value dv;
        dv["class_id"] = d.class_id;
        dv["score"] = d.score;
        dv["x"] = d.box.x;
        dv["y"] = d.box.y;
        dv["w"] = d.box.w;
        dv["h"] = d.box.h;
        dets.push_back(dv);
    }
    v["detections"] = dets;
    return json::dump(v, 0);
}

bool EngineRuntime::DueForSnapshot(int64_t now_ms) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!running_) return false;
    if (now_ms - last_snapshot_ms_ >= 1000) {
        last_snapshot_ms_ = now_ms;
        return true;
    }
    return false;
}

// ---------------- Watchdog ----------------
Watchdog::Watchdog(int max_restarts, int64_t window_ms)
    : max_restarts_(max_restarts), window_ms_(window_ms) {}

void Watchdog::Prune(int64_t now_ms) const {
    while (!restarts_.empty() && (now_ms - restarts_.front()) > window_ms_) {
        restarts_.pop_front();
    }
}

bool Watchdog::AllowRestart(int64_t now_ms) {
    Prune(now_ms);
    if (static_cast<int>(restarts_.size()) >= max_restarts_) return false;
    restarts_.push_back(now_ms);
    return true;
}

int Watchdog::RestartCount(int64_t now_ms) const {
    Prune(now_ms);
    return static_cast<int>(restarts_.size());
}

} // namespace aim
