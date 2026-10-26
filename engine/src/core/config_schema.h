#pragma once
#include <string>
#include "core/result.h"

// 配置 JSON Schema v3 校验（§8.2 子集）：直接对 JSON 文本校验，
// 与 ConfigManager::Validate（结构体层）互补，供 UI/IPC 入口复用。
namespace aim {

class ConfigSchema {
public:
    static Result<void> ValidateJson(const std::string& text);

    static bool IsIpv4(const std::string& s);
    static bool IsHex8(const std::string& s);
};

} // namespace aim
