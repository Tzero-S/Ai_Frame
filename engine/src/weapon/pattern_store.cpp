#include "weapon/pattern_store.h"
#include "core/json.h"
#include <algorithm>

namespace aim {

bool PatternStore::LoadFromJson(const std::string& text, std::string* err) {
    json::Value root;
    std::string e;
    if (!json::parse(text, root, e)) {
        if (err) *err = "弹道 JSON 解析失败: " + e;
        return false;
    }
    const json::Value* arr = root.find("patterns");
    if (!arr || !arr->isArray()) {
        if (err) *err = "缺少 patterns 数组";
        return false;
    }
    patterns_.clear();
    for (const auto& pv : arr->arr) {
        RecoilPattern p;
        p.id = pv["id"].asString();
        if (p.id.empty()) continue;
        const json::Value* samples = pv.find("samples");
        if (samples && samples->isArray()) {
            for (const auto& sv : samples->arr) {
                PatternSample s;
                s.t_ms = static_cast<float>(sv["t"].asNumber(0.0));
                s.dx = static_cast<float>(sv["dx"].asNumber(0.0));
                s.dy = static_cast<float>(sv["dy"].asNumber(0.0));
                p.samples.push_back(s);
            }
            std::sort(p.samples.begin(), p.samples.end(),
                      [](const PatternSample& a, const PatternSample& b) { return a.t_ms < b.t_ms; });
        }
        patterns_.push_back(std::move(p));
    }
    return true;
}

std::string PatternStore::ToJson() const {
    json::Value root;
    json::Value arr;
    for (const auto& p : patterns_) {
        json::Value pv;
        pv["id"] = p.id;
        json::Value samples;
        for (const auto& s : p.samples) {
            json::Value sv;
            sv["t"] = s.t_ms;
            sv["dx"] = s.dx;
            sv["dy"] = s.dy;
            samples.push_back(sv);
        }
        pv["samples"] = samples;
        arr.push_back(pv);
    }
    root["patterns"] = arr;
    return json::dump(root, 2);
}

bool PatternStore::Has(const std::string& id) const { return Get(id) != nullptr; }

const RecoilPattern* PatternStore::Get(const std::string& id) const {
    for (const auto& p : patterns_) if (p.id == id) return &p;
    return nullptr;
}

void PatternStore::Add(const RecoilPattern& p) {
    for (auto& existing : patterns_) {
        if (existing.id == p.id) { existing = p; return; }
    }
    patterns_.push_back(p);
}

Vec2F PatternStore::Sample(const std::string& id, float t_ms) const {
    const RecoilPattern* p = Get(id);
    if (!p || p->samples.empty()) return Vec2F{};

    const auto& s = p->samples;
    if (t_ms <= s.front().t_ms) return Vec2F{ s.front().dx, s.front().dy };
    if (t_ms >= s.back().t_ms)  return Vec2F{ s.back().dx, s.back().dy };

    // 线性插值
    for (size_t i = 1; i < s.size(); ++i) {
        if (t_ms <= s[i].t_ms) {
            const float span = s[i].t_ms - s[i - 1].t_ms;
            const float u = (span > 0.f) ? (t_ms - s[i - 1].t_ms) / span : 0.f;
            return Vec2F{
                s[i - 1].dx + u * (s[i].dx - s[i - 1].dx),
                s[i - 1].dy + u * (s[i].dy - s[i - 1].dy)
            };
        }
    }
    return Vec2F{ s.back().dx, s.back().dy };
}

} // namespace aim
