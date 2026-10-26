#include "core/config_store.h"

#include <filesystem>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace aim {

namespace {

// 深合并：对象逐键递归；数组/标量整体替换（数组语义为"列表"，不应按下标合并）。
void MergeInto(json::Value& base, const json::Value& patch) {
    if (!base.isObject() || !patch.isObject()) {
        base = patch;
        return;
    }
    for (const auto& kv : patch.obj) {
        json::Value* slot = base.find(kv.first);
        if (!slot) {
            base.obj.push_back(kv);
            continue;
        }
        MergeInto(*slot, kv.second);
    }
}

} // namespace

ConfigStore::ConfigStore() : cfg_(ConfigManager::Defaults()) {}

ConfigStore::ConfigStore(const Config& initial) : cfg_(initial) {}

Config ConfigStore::Get() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return cfg_;
}

int ConfigStore::Version() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return version_;
}

Result<int> ConfigStore::Apply(const json::Value& v) {
    return ApplyJson(json::dump(v, 0));
}

Result<int> ConfigStore::ApplyJson(const std::string& text) {
    auto parsed = ConfigManager::FromJson(text);
    if (!parsed.ok()) {
        return Result<int>::Fail(parsed.error().code, parsed.error().message, parsed.error().advice);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    cfg_ = parsed.value();
    ++version_;
    return Result<int>::Ok(version_);
}

Result<int> ConfigStore::ApplyMerge(const json::Value& patch) {
    return ApplyMergeJson(json::dump(patch, 0));
}

Result<int> ConfigStore::ApplyMergeJson(const std::string& patch_text) {
    json::Value patch;
    std::string err;
    if (!json::parse(patch_text, patch, err)) {
        return Result<int>::Fail(3001, "配置 patch 解析失败: " + err, "检查 JSON 语法");
    }
    if (!patch.isObject()) {
        return Result<int>::Fail(3002, "配置 patch 必须是 JSON 对象", "提交形如 {\"aim\":{...}} 的对象");
    }

    // 读-改-写全程持锁，避免并发 apply 造成丢失更新（ApplyJson 会二次加锁，故此处内联）
    std::lock_guard<std::mutex> lk(mtx_);

    json::Value base;
    if (!json::parse(ConfigManager::ToJson(cfg_, 0), base, err)) {
        return Result<int>::Fail(3003, "当前配置序列化失败: " + err, "内部错误，请重试");
    }
    MergeInto(base, patch);

    // 合并结果同样走 migrate + validate（非法值不会落库）
    auto parsed = ConfigManager::FromJson(json::dump(base, 0));
    if (!parsed.ok()) {
        return Result<int>::Fail(parsed.error().code, parsed.error().message, parsed.error().advice);
    }
    cfg_ = parsed.value();
    ++version_;
    return Result<int>::Ok(version_);
}

Result<void> ConfigStore::SaveToFile(const std::string& path) const {
    Config copy;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        copy = cfg_;
    }

    const std::string tmp = path + ".tmp";
    auto saved = ConfigManager::SaveToFile(tmp, copy);
    if (!saved.ok()) return saved;

#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return Result<void>::Fail(1010, "rename config failed",
                                  "检查目标目录权限与磁盘空间");
    }
#else
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return Result<void>::Fail(1010, "rename config failed", "检查目标目录权限");
    }
#endif
    return Result<void>::Ok();
}

Result<void> ConfigStore::LoadFromFile(const std::string& path) {
    auto loaded = ConfigManager::LoadFromFile(path);
    if (!loaded.ok()) {
        return Result<void>::Fail(loaded.error().code, loaded.error().message,
                                  loaded.error().advice);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    cfg_ = loaded.value();
    ++version_;
    return Result<void>::Ok();
}

} // namespace aim
