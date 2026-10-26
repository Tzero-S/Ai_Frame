#pragma once

// L1 行为层：目标切换迟滞 + 无目标自适应降频（§5.12.1）。
namespace aim::opsec {

class TargetHysteresis {
public:
    void Configure(int required_frames);
    // 返回 true 表示允许从 current_id 切换到 new_id。
    bool AllowSwitch(int new_id, int current_id);
    void Reset();

private:
    int required_ = 2;
    int candidate_ = -1;
    int streak_ = 0;
};

class IdlePacer {
public:
    void Configure(int active_fps, int idle_fps, int idle_after_frames = 10);
    // 返回本帧后需要 sleep 的毫秒数（0 = 不等待）。
    int NextDelayMs(bool has_target);
    void Reset();

private:
    int active_fps_ = 120;
    int idle_fps_ = 30;
    int idle_after_ = 10;
    int idle_frames_ = 0;
};

} // namespace aim::opsec
