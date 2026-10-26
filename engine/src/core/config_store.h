#pragma once
#include <mutex>
#include <string>
#include "core/config.h"
#include "core/json.h"
#include "core/result.h"

// 配置中心运行时（§5.1.3）：
//   - Apply：整份替换。迁移 + 校验通过后才原子替换，并递增 applied_version
//   - ApplyMerge：深合并提交。patch 中出现的字段覆盖、未出现的字段保留现值
//     （控制台只渲染部分分组时，避免把 anti_detect / capture 等节静默重置为默认）
//   - SaveToFile：写临时文件 → 校验 → rename（原子落盘）
namespace aim {

class ConfigStore {
public:
    ConfigStore();
    explicit ConfigStore(const Config& initial);

    Config Get() const;
    int Version() const;

    // 整份替换：缺失的分组回落默认值（保持 §5.1.3 原始语义）
    Result<int> Apply(const json::Value& v);
    Result<int> ApplyJson(const std::string& text);

    // 深合并：对象递归合并、数组/标量整体替换；结果同样经迁移 + 校验
    Result<int> ApplyMerge(const json::Value& patch);
    Result<int> ApplyMergeJson(const std::string& patch_text);

    Result<void> SaveToFile(const std::string& path) const;
    Result<void> LoadFromFile(const std::string& path);

private:
    mutable std::mutex mtx_;
    Config cfg_;
    int version_ = 0;
};

} // namespace aim
