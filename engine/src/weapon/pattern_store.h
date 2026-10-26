#pragma once
#include <string>
#include <vector>
#include "core/types.h"

// 弹道库（§5.7.1）：每武器一条曲线，采样为 [t_ms][dx,dy]，支持 JSON 导入/导出。
namespace aim {

struct PatternSample {
    float t_ms = 0.f;
    float dx = 0.f;
    float dy = 0.f;
};

struct RecoilPattern {
    std::string id;
    std::vector<PatternSample> samples; // 按 t_ms 升序
};

class PatternStore {
public:
    // JSON 形如 {"patterns":[{"id":"m4a1s","samples":[{"t":0,"dx":0,"dy":1.2}]}]}
    bool LoadFromJson(const std::string& text, std::string* err = nullptr);
    std::string ToJson() const;

    bool Has(const std::string& id) const;
    const RecoilPattern* Get(const std::string& id) const;
    void Add(const RecoilPattern& p);

    // 线性插值采样；缺失返回 {0,0}。
    Vec2F Sample(const std::string& id, float t_ms) const;

    size_t Count() const { return patterns_.size(); }
    void Clear() { patterns_.clear(); }

private:
    std::vector<RecoilPattern> patterns_;
};

} // namespace aim
