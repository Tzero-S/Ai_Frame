#pragma once
#include <string>
#include "core/types.h"
#include "weapon/pattern_store.h"

// 压枪（§5.7.1 / §7.5）
//
// 量纲约定（重要）：
//   CumulativeAt(t) 返回"从开火到 t 时刻累计应下压多少像素"（绝对量）
//   SampleNow()     返回"本帧相对上一帧的新增量"（增量）
//   调用方（RunLoop → Pipeline）拿到的是**增量**，直接并入每帧位移。
//   早期实现把累计量当增量用，导致补偿量随帧率线性放大，已修正。
//
// 横向：
//   manual_horizontal = true 时，横向 = 垂直 × horizontal_gain
//     （用户拖一个滑条定方向与幅度，对应"左/右偏移"UI）
//   manual_horizontal = false 时，使用曲线自带的 dx（实录曲线）
//   实测 9 段实录均为"单向斜飞"，一个带符号比值即可表达，无需逐枪建模。
namespace aim {

struct RecoilParams {
    float strength = 5.f;        // [0,30] 强度 → 峰值补偿比例（15 = base_px）
    int rpm = 600;               // 武器射速
    float k = 0.2f;              // 无弹道库时的"每发垂直补偿"经验系数
    float base_px = 200.f;       // 曲线峰值(shape=1.0)对应的总补偿像素（strength=15 时）
    float horizontal_gain = 0.f; // 横向增益 [-1.5,1.5]；0=只压垂直，负=向左
    bool manual_horizontal = true; // true=用 gain 覆盖曲线横向
};

class RecoilEngine {
public:
    void Configure(const RecoilParams& p,
                   const PatternStore* store = nullptr,
                   const std::string& pattern_id = "");

    float FireIntervalMs() const;
    // 峰值总补偿像素（strength=15 时），用于 UI 显示与标定
    float BaseCompensation() const;

    // t 时刻的累计补偿量（像素，正=右/下）
    Vec2F CumulativeAt(float t_ms) const;
    // 兼容旧名：等价于 CumulativeAt
    Vec2F CompensationAt(float t_ms) const { return CumulativeAt(t_ms); }

    // 本帧增量（相对上次调用）。首次调用从 0 起算。
    Vec2F SampleNow();

    // 运行时改强度（用于按开镜倍率切换 strength_ads，无需重建引擎）
    void SetStrength(float s) { p_.strength = s; }
    void Advance(float dt_ms) { t_ += dt_ms; }
    void OnShot() { t_ += FireIntervalMs(); }
    void Reset() { t_ = 0.f; last_cum_ = Vec2F{}; have_last_ = false; }
    float ElapsedMs() const { return t_; }

private:
    RecoilParams p_;
    const PatternStore* store_ = nullptr;
    std::string pattern_id_;
    float t_ = 0.f;
    Vec2F last_cum_{};
    bool have_last_ = false;
};

} // namespace aim
