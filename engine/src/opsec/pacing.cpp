#include "opsec/pacing.h"

#include <algorithm>

namespace aim::opsec {

void TargetHysteresis::Configure(int required_frames) {
    required_ = std::max(0, required_frames);
    Reset();
}

bool TargetHysteresis::AllowSwitch(int new_id, int current_id) {
    if (new_id == current_id) {
        candidate_ = -1;
        streak_ = 0;
        return true;
    }
    if (required_ <= 0) return true;

    if (new_id != candidate_) {
        candidate_ = new_id;
        streak_ = 1;
    } else {
        ++streak_;
    }

    if (streak_ >= required_) {
        candidate_ = -1;
        streak_ = 0;
        return true;
    }
    return false;
}

void TargetHysteresis::Reset() {
    candidate_ = -1;
    streak_ = 0;
}

void IdlePacer::Configure(int active_fps, int idle_fps, int idle_after_frames) {
    active_fps_ = std::max(1, active_fps);
    idle_fps_ = std::max(1, idle_fps);
    idle_after_ = std::max(0, idle_after_frames);
    Reset();
}

int IdlePacer::NextDelayMs(bool has_target) {
    if (has_target) {
        idle_frames_ = 0;
        return 0;
    }
    ++idle_frames_;
    if (idle_frames_ < idle_after_) return 0;
    return std::max(1, 1000 / idle_fps_);
}

void IdlePacer::Reset() { idle_frames_ = 0; }

} // namespace aim::opsec
