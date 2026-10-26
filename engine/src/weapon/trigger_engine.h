#pragma once
#include <string>
#include "core/types.h"
#include "core/config.h"

// 扳机（§5.7.2）：
//   - trigger.key 未设置 → 扳机整体不生效（UI 红色提示）
//   - 步枪(连发)：命中后延时 rifle_delay_ms(70) 按住开火并跟随
//   - 狙击(点射)：命中后延时 sniper_delay_ms 单发点射并复位
//   - 命中判定：准星落入目标框、score ≥ conf_threshold、且在 scope 内（scope 由上游过滤）
namespace aim {

constexpr float kHitBandRatio = 0.10f; // 判定线纵向容差（框高的 10%）
constexpr float kSniperPulseMs = 30.f; // 狙击单发脉冲宽度

struct TriggerParams {
    bool enabled = false;
    TriggerType type = TriggerType::Rifle;
    std::string key;
    bool scope_follow_aim = true;
    int scope_half_w = 320;
    int scope_half_h = 240;
    int aim_offset_px = 0;
    int rifle_delay_ms = 70;
    int sniper_delay_ms = 0;
    float conf_threshold = 0.35f;
};

struct TriggerInput {
    bool key_down = false;     // 扳机键是否按下
    bool crosshair_hit = false; // 命中判定（由 CrosshairHits 计算）
    float target_score = 0.f;
};

struct TriggerOutput {
    bool fire_down = false;
    bool fire_up = false;
};

// 命中判定（§5.7.2）：score 达标 + 准星在框内 + 距判定线（框中心 + aim_offset）在容差内。
bool CrosshairHits(const Detection& d, float crosshair_x, float crosshair_y,
                   int aim_offset_px, float conf_threshold,
                   float band_ratio = kHitBandRatio);

class TriggerEngine {
public:
    void Configure(const TriggerParams& p);
    TriggerOutput Update(const TriggerInput& in, float dt_ms);
    void Reset();
    bool Firing() const { return firing_; }

private:
    TriggerParams p_;
    bool armed_ = false;
    bool firing_ = false;
    float elapsed_ = 0.f;     // 从首次命中起的累计
    float shot_elapsed_ = 0.f; // 狙击脉冲计时
};

} // namespace aim
