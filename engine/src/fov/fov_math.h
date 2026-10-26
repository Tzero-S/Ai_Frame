#pragma once
#include <cmath>

// 视场 / 像素↔角度数学（§5.8.2 / §7.1）。纯数学，无平台依赖，便于单测与瞄准层复用。
namespace aim {

constexpr float kPi = 3.14159265358979323846f;
constexpr float Deg2Rad(float d) { return d * kPi / 180.f; }
constexpr float Rad2Deg(float r) { return r * 180.f / kPi; }

// §5.8.2 查表：宽高比 aspect=w/h -> 水平 FOV(度)。
//   4:3→90°, 16:10→97°, 16:9→103°, 21:9→118°；中间线性插值。
//
// 锚点必须**随 aspect 单调不减**：更宽的屏幕不可能有更窄的水平视场。
// 历史 bug：16:10 锚点曾写成 110°，导致 1.6 处的 HFOV 反而比 1.7778（103°）大，
// 曲线在 (1.60, 1.78) 区间有 18 个下降点，且 16:10 屏的 VFOV 被算成 83.5°
// （16:9 是 70.5°，同一款游戏不可能只因为屏幕比例不同就多出 13° 垂直视场）。
// 97° 由 16:9 的 103° 反推：2·atan(tan(103°/2)·1.6/1.7778) ≈ 97.0°。
inline float HfovFromAspect(float aspect) {
    if (aspect <= 1.3333333f) return 90.f;                                   // ≤4:3
    if (aspect <= 1.6f) {                                                    // 4:3 → 16:10
        float t = (aspect - 1.3333333f) / (1.6f - 1.3333333f);
        return 90.f + t * (97.f - 90.f);
    }
    if (aspect <= 1.7777778f) {                                              // 16:10 → 16:9
        float t = (aspect - 1.6f) / (1.7777778f - 1.6f);
        return 97.f + t * (103.f - 97.f);
    }
    if (aspect <= 2.3333333f) {                                              // 16:9 → 21:9
        float t = (aspect - 1.7777778f) / (2.3333333f - 1.7777778f);
        return 103.f + t * (118.f - 103.f);
    }
    return 118.f;                                                            // ≥21:9
}

// §5.8.2：vfov = 2·atan(tan(hfov/2)/aspect)
inline float VfovFromHfov(float hfov_deg, float aspect) {
    float hf = Deg2Rad(hfov_deg);
    return Rad2Deg(2.f * std::atan(std::tan(hf * 0.5f) / aspect));
}

// 视场状态：持有水平/垂直 FOV（度），提供焦距与角度换算。
struct Fov {
    float hfov_deg = 103.f;
    float vfov_deg = 59.f;

    // §7.1：f = (W/2) / tan(hfov/2)
    float FocalX(float screen_w) const {
        return (screen_w * 0.5f) / std::tan(Deg2Rad(hfov_deg) * 0.5f);
    }
    float FocalY(float screen_h) const {
        return (screen_h * 0.5f) / std::tan(Deg2Rad(vfov_deg) * 0.5f);
    }
};

// §7.1：angle = atan(dx / f)（准星中心为原点）
inline float AngleX(float dx, float fx) { return std::atan(dx / fx); }
inline float AngleY(float dy, float fy) { return std::atan(dy / fy); }

} // namespace aim
