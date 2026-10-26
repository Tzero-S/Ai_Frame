#pragma once
#include <string>
#include <variant>
#include <utility>

// 错误处理规范（§9.2）：引擎侧跨模块返回统一用 Result<T,Error>，不靠异常跨模块逃逸。
namespace aim {

struct Error {
    int code = 0;
    std::string message;
    // 可执行的用户建议（§9.2：每处错误必须带可执行的用户建议）
    std::string advice;
};

template <typename T>
class Result {
public:
    Result(T value) : v_(std::move(value)) {}
    Result(Error e) : v_(std::move(e)) {}

    bool ok() const noexcept { return std::holds_alternative<T>(v_); }
    const T& value() const { return std::get<T>(v_); }
    T& value() { return std::get<T>(v_); }
    const Error& error() const { return std::get<Error>(v_); }

    static Result<T> Ok(T v) { return Result<T>(std::move(v)); }
    static Result<T> Fail(int code, std::string msg, std::string advice = "") {
        return Result<T>(Error{ code, std::move(msg), std::move(advice) });
    }

private:
    std::variant<T, Error> v_;
};

// void 特化：std::variant<void, Error> 非法（void 非对象类型），单独实现。
template <>
class Result<void> {
public:
    Result() : ok_(true) {}                                 // 默认构造 = 成功
    Result(Error e) : ok_(false), err_(std::move(e)) {}

    bool ok() const noexcept { return ok_; }
    const Error& error() const { return err_; }

    static Result<void> Ok() { return Result<void>(); }
    static Result<void> Fail(int code, std::string msg, std::string advice = "") {
        return Result<void>(Error{ code, std::move(msg), std::move(advice) });
    }

private:
    bool ok_ = true;
    Error err_;
};

} // namespace aim
