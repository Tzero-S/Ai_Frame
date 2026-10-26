#pragma once
#include "core/types.h"

// 平滑（§5.5.4）：把"每帧的原始位移指令"变成连续轨迹。
//
// 两种模型：
//   ema      —— 一阶 EMA，state += alpha*(in-state)。实现简单，但大角度转向会过冲/滞后。
//   critical —— 临界阻尼二阶系统（ζ=1），无过冲、速度连续；可选 jerk 限幅。
//
// 注意：首次调用直接返回输入（不产生启动延迟），保证单帧场景（预览/测试）行为不变。
namespace aim {

enum class SmoothingModel { Ema, Critical };

struct SmoothingFilter {
    SmoothingModel model = SmoothingModel::Critical;

    // ema 模式参数
    float alpha = 0.35f;

    // critical 模式参数
    float omega = 25.f;   // 自然频率 rad/s，越大越跟手
    float max_jerk = 0.f; // 加速度变化率上限 px/s³，0 = 不限

    Vec2F state{};
    Vec2F vel{};
    Vec2F last_acc{};
    bool init = false;

    // dt_s：距上一帧的秒数（<=0 时按 1/120s 处理）
    Vec2F Apply(const Vec2F& in, float dt_s = 1.f / 120.f);

    void Reset() {
        state = Vec2F{};
        vel = Vec2F{};
        last_acc = Vec2F{};
        init = false;
    }
};

} // namespace aim
