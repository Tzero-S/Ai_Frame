#include "aim/smoothing.h"

#include <algorithm>
#include <cmath>

namespace aim {

namespace {
float ClampAbs(float v, float limit) {
    if (limit <= 0.f) return v;
    return std::max(-limit, std::min(limit, v));
}
} // namespace

Vec2F SmoothingFilter::Apply(const Vec2F& in, float dt_s) {
    if (!(dt_s > 0.f) || !std::isfinite(dt_s)) dt_s = 1.f / 120.f;
    // 单帧抖动保护：异常大的 dt（掉帧/断点）直接钳到 100ms，避免一步跳死
    dt_s = std::min(dt_s, 0.1f);

    if (!init) {
        // 首帧直通：不引入启动延迟
        state = in;
        vel = Vec2F{};
        last_acc = Vec2F{};
        init = true;
        return state;
    }

    if (model == SmoothingModel::Ema) {
        const float a = std::max(0.01f, std::min(1.f, alpha));
        state.x += a * (in.x - state.x);
        state.y += a * (in.y - state.y);
        return state;
    }

    // 临界阻尼：a = -2ω·v - ω²·(x - in)
    // 显式欧拉要求 ω·dt 足够小，否则发散；按 ω·h ≤ 0.25 细分步长。
    const float w = std::max(0.1f, omega);
    const float max_h = 0.25f / w;
    int steps = static_cast<int>(std::ceil(dt_s / max_h));
    steps = std::max(1, std::min(steps, 64));
    const float h = dt_s / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        Vec2F acc{
            -2.f * w * vel.x - w * w * (state.x - in.x),
            -2.f * w * vel.y - w * w * (state.y - in.y),
        };
        // jerk 限幅：限制加速度的每步变化量
        if (max_jerk > 0.f) {
            const float dj = max_jerk * h;
            acc.x = last_acc.x + ClampAbs(acc.x - last_acc.x, dj);
            acc.y = last_acc.y + ClampAbs(acc.y - last_acc.y, dj);
        }
        vel.x += acc.x * h;
        vel.y += acc.y * h;
        state.x += vel.x * h;
        state.y += vel.y * h;
        last_acc = acc;
    }

    return state;
}

} // namespace aim
