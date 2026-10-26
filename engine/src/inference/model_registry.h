#pragma once
#include <string>
#include <vector>

// 模型注册表（§5.10.1 / §5.10.3）：models.json 的读写与查询。
namespace aim {

struct ModelEntry {
    std::string id;
    std::string alias;
    std::string file;
    std::string sha256;
    std::vector<std::string> classes;
    int num_kpts = 0;
    std::string created_at;
};

class ModelRegistry {
public:
    // JSON 形如 {"models":[{ "id":..., "alias":..., "classes":[...] }]}
    bool LoadFromJson(const std::string& text, std::string* err = nullptr);
    std::string ToJson() const;

    void Add(const ModelEntry& e);
    const ModelEntry* Get(const std::string& id) const;
    const ModelEntry* FindByAlias(const std::string& alias) const;

    size_t Count() const { return models_.size(); }
    void Clear() { models_.clear(); }

private:
    std::vector<ModelEntry> models_;
};

} // namespace aim
