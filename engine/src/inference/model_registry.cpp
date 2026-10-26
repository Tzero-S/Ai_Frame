#include "inference/model_registry.h"
#include "core/json.h"

namespace aim {

bool ModelRegistry::LoadFromJson(const std::string& text, std::string* err) {
    json::Value root;
    std::string e;
    if (!json::parse(text, root, e)) {
        if (err) *err = "模型注册表 JSON 解析失败: " + e;
        return false;
    }
    const json::Value* arr = root.find("models");
    if (!arr || !arr->isArray()) {
        if (err) *err = "缺少 models 数组";
        return false;
    }
    models_.clear();
    for (const auto& mv : arr->arr) {
        ModelEntry m;
        m.id = mv["id"].asString();
        if (m.id.empty()) continue;
        m.alias = mv["alias"].asString();
        m.file = mv["file"].asString();
        m.sha256 = mv["sha256"].asString();
        m.num_kpts = mv["num_kpts"].asInt(0);
        m.created_at = mv["created_at"].asString();
        const json::Value* cls = mv.find("classes");
        if (cls && cls->isArray()) {
            for (const auto& c : cls->arr) m.classes.push_back(c.asString());
        }
        models_.push_back(std::move(m));
    }
    return true;
}

std::string ModelRegistry::ToJson() const {
    json::Value root;
    json::Value arr;
    for (const auto& m : models_) {
        json::Value mv;
        mv["id"] = m.id;
        mv["alias"] = m.alias;
        mv["file"] = m.file;
        mv["sha256"] = m.sha256;
        json::Value cls;
        for (const auto& c : m.classes) cls.push_back(json::Value(c));
        mv["classes"] = cls;
        mv["num_kpts"] = m.num_kpts;
        mv["created_at"] = m.created_at;
        arr.push_back(mv);
    }
    root["models"] = arr;
    return json::dump(root, 2);
}

void ModelRegistry::Add(const ModelEntry& e) {
    for (auto& existing : models_) {
        if (existing.id == e.id) { existing = e; return; }
    }
    models_.push_back(e);
}

const ModelEntry* ModelRegistry::Get(const std::string& id) const {
    for (const auto& m : models_) if (m.id == id) return &m;
    return nullptr;
}

const ModelEntry* ModelRegistry::FindByAlias(const std::string& alias) const {
    for (const auto& m : models_) if (m.alias == alias) return &m;
    return nullptr;
}

} // namespace aim
