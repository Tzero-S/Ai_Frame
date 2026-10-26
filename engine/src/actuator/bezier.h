#pragma once
#include <vector>
#include "core/types.h"
#include "core/config.h"

// 贝塞尔曲线（§5.6.3 / §7.4）：把总位移按曲线分配到 N 个 tick。
//
// 口径（§5.6.3 已修正）：控制点比例按强度线性插值——
//   s=0 → 精确线性 (1/3, 2/3)；s=1 → 表中该类型控制点
//   （ease_in 0.25/0.75、ease_out 0.75/0.25、smoothing 0.42/0.58）。
// 早期草稿的 k=0.25+0.5·s 在 s=0 时不能退化为线性，已废弃。
namespace aim {

struct BezierSpec {
    BezierType type = BezierType::EaseIn;
    float strength = 0.5f; // [0,1]
};

// 控制点比例（相对 P0→P3 位移 D）
void BezierFractions(BezierType type, float strength, float& p1_frac, float& p2_frac);

// 曲线上的点 B(t)，t∈[0,1]
Vec2F BezierPoint(const Vec2F& from, const Vec2F& to, float t, const BezierSpec& spec);

// 将总位移切成 ticks 个增量（相邻采样点差值），用于逐 tick 下发。
std::vector<Vec2F> BezierTicks(const Vec2F& from, const Vec2F& to,
                               int ticks, const BezierSpec& spec);

} // namespace aim
