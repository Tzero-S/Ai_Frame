#include "actuator/bezier.h"
#include <algorithm>

namespace aim {

namespace {
constexpr float kLinearP1 = 1.f / 3.f; // 精确线性的控制点
constexpr float kLinearP2 = 2.f / 3.f;

void TypeFractions(BezierType type, float& f1, float& f2) {
    switch (type) {
        case BezierType::EaseIn:    f1 = 0.25f; f2 = 0.75f; break; // 借入
        case BezierType::EaseOut:   f1 = 0.75f; f2 = 0.25f; break; // 偏出
        case BezierType::Smoothing: f1 = 0.42f; f2 = 0.58f; break; // 缓和
    }
}
} // namespace

void BezierFractions(BezierType type, float strength, float& p1_frac, float& p2_frac) {
    float f1, f2;
    TypeFractions(type, f1, f2);
    const float s = std::clamp(strength, 0.f, 1.f);
    p1_frac = kLinearP1 + s * (f1 - kLinearP1);
    p2_frac = kLinearP2 + s * (f2 - kLinearP2);
}

Vec2F BezierPoint(const Vec2F& from, const Vec2F& to, float t, const BezierSpec& spec) {
    float p1f, p2f;
    BezierFractions(spec.type, spec.strength, p1f, p2f);

    const Vec2F p0 = from;
    const Vec2F p3 = to;
    const Vec2F p1{ p0.x + p1f * (p3.x - p0.x), p0.y + p1f * (p3.y - p0.y) };
    const Vec2F p2{ p0.x + p2f * (p3.x - p0.x), p0.y + p2f * (p3.y - p0.y) };

    const float u = 1.f - t;
    const float b0 = u * u * u;
    const float b1 = 3.f * u * u * t;
    const float b2 = 3.f * u * t * t;
    const float b3 = t * t * t;

    return Vec2F{
        b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
        b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y
    };
}

std::vector<Vec2F> BezierTicks(const Vec2F& from, const Vec2F& to,
                               int ticks, const BezierSpec& spec) {
    std::vector<Vec2F> out;
    if (ticks <= 0) return out;
    out.reserve(static_cast<size_t>(ticks));

    Vec2F prev = from;
    for (int i = 1; i <= ticks; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(ticks);
        const Vec2F p = BezierPoint(from, to, t, spec);
        out.push_back(Vec2F{ p.x - prev.x, p.y - prev.y });
        prev = p;
    }
    return out;
}

} // namespace aim
