#pragma once
#include <string>
#include <vector>
#include <utility>

// 最小 JSON 实现：覆盖本框架配置/协议所需子集（对象/数组/字符串/数字/布尔/null、
// 转义、嵌套）。用于在不引入第三方库（nlohmann/json 当前未安装）的情况下实现
// §5.1 配置 Schema 的序列化与 §8.1 IPC 报文的往返。接口刻意贴近 nlohmann 习惯，
// 后续接入第三方库时可低成本替换。
namespace aim::json {

enum class JType { Null, Bool, Number, String, Array, Object };

struct Value {
    JType type = JType::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    // 便捷构造（nlohmann 风格，便于序列化）
    Value() = default;
    Value(std::nullptr_t) : type(JType::Null) {}
    Value(bool v) : type(JType::Bool), b(v) {}
    Value(int v) : type(JType::Number), num(static_cast<double>(v)) {}
    Value(long v) : type(JType::Number), num(static_cast<double>(v)) {}
    Value(double v) : type(JType::Number), num(v) {}
    Value(const char* v) : type(JType::String), str(v) {}
    Value(const std::string& v) : type(JType::String), str(v) {}
    Value(std::string&& v) : type(JType::String), str(std::move(v)) {}

    Value& operator=(std::nullptr_t) { reset(JType::Null); return *this; }
    Value& operator=(bool v) { reset(JType::Bool); b = v; return *this; }
    Value& operator=(int v) { reset(JType::Number); num = static_cast<double>(v); return *this; }
    Value& operator=(long v) { reset(JType::Number); num = static_cast<double>(v); return *this; }
    Value& operator=(double v) { reset(JType::Number); num = v; return *this; }
    Value& operator=(const char* v) { reset(JType::String); str = v; return *this; }
    Value& operator=(const std::string& v) { reset(JType::String); str = v; return *this; }

    void reset(JType t) { type = t; b = false; num = 0.0; str.clear(); arr.clear(); obj.clear(); }

    // 数组追加（自动把类型切为 Array，避免直接操作 arr 忘记设类型）
    void push_back(const Value& v) { if (!isArray()) reset(JType::Array); arr.push_back(v); }
    void push_back(Value&& v) { if (!isArray()) reset(JType::Array); arr.push_back(std::move(v)); }

    bool isNull() const noexcept { return type == JType::Null; }
    bool isBool() const noexcept { return type == JType::Bool; }
    bool isNumber() const noexcept { return type == JType::Number; }
    bool isString() const noexcept { return type == JType::String; }
    bool isArray() const noexcept { return type == JType::Array; }
    bool isObject() const noexcept { return type == JType::Object; }

    bool asBool(bool def = false) const noexcept { return isBool() ? b : def; }
    double asNumber(double def = 0.0) const noexcept { return isNumber() ? num : def; }
    int asInt(int def = 0) const noexcept { return isNumber() ? static_cast<int>(num) : def; }
    long asLong(long def = 0) const noexcept { return isNumber() ? static_cast<long>(num) : def; }
    std::string asString(const std::string& def = "") const noexcept { return isString() ? str : def; }

    const Value* find(const std::string& key) const {
        if (!isObject()) return nullptr;
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    Value* find(const std::string& key) {
        if (!isObject()) return nullptr;
        for (auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    const Value& at(const std::string& key) const {
        static const Value kNull;
        const Value* v = find(key);
        return v ? *v : kNull;
    }
    // 对象索引（可变：缺失则插入）
    Value& operator[](const std::string& key) {
        if (!isObject()) { type = JType::Object; obj.clear(); }
        for (auto& kv : obj) if (kv.first == key) return kv.second;
        obj.emplace_back(key, Value{});
        return obj.back().second;
    }
    // 对象索引（只读）
    const Value& operator[](const std::string& key) const {
        const Value* v = find(key);
        static const Value kNull;
        return v ? *v : kNull;
    }
    // 数组索引（可变：自动扩容）
    Value& operator[](std::size_t idx) {
        if (!isArray()) { type = JType::Array; arr.clear(); }
        if (idx >= arr.size()) arr.resize(idx + 1);
        return arr[idx];
    }
    // 数组索引（只读）
    const Value& operator[](std::size_t idx) const {
        static const Value kNull;
        if (!isArray() || idx >= arr.size()) return kNull;
        return arr[idx];
    }
    std::size_t size() const noexcept {
        return isArray() ? arr.size() : (isObject() ? obj.size() : 0);
    }
};

// 解析：失败返回 false 并写 err（§9.2 错误须带可执行建议 —— 调用方可据 err 提示用户）
bool parse(const std::string& text, Value& out, std::string& err);
Value parseOrThrow(const std::string& text);

// 序列化：indent>0 时美化输出（含换行与缩进），否则紧凑单行。
std::string dump(const Value& v, int indent = 0);

} // namespace aim::json
