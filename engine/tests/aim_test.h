#pragma once
// 轻量单元测试框架（替代未安装的 GoogleTest）。
// 偏离说明：文档 §10 质量保障默认以 GoogleTest 为单测框架；本环境未安装，
// 这里提供接口近似（AIM_TEST / AIM_EXPECT_EQ / AIM_EXPECT_NEAR / AIM_EXPECT_TRUE）
// 的最小实现，可在接入 GoogleTest 时低成本迁移。

#include <string>
#include <vector>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <functional>

namespace aim { namespace test {

using Fn = void(*)();
inline std::vector<std::pair<std::string, Fn>>& registry() {
    static std::vector<std::pair<std::string, Fn>> r;
    return r;
}
inline int& failures() { static int f = 0; return f; }
inline int& ran() { static int n = 0; return n; }

inline void register_case(const std::string& name, Fn f) {
    registry().push_back({name, f});
}

inline void record(const std::string& name, bool ok, const std::string& detail = "") {
    ++ran();
    if (!ok) {
        ++failures();
        std::fprintf(stderr, "  [FAIL] %s%s%s\n", name.c_str(),
                     detail.empty() ? "" : " -> ", detail.c_str());
    }
}

template <typename T>
concept Streamable = requires(std::ostream& os, const T& v) { os << v; };

template <Streamable T>
inline std::string to_str(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}
template <typename T>
inline std::string to_str(const T&) { return "<non-streamable>"; }

inline int run_all() {
    std::fprintf(stdout, "=== aim::test run (%zu cases) ===\n", registry().size());
    for (auto& kv : registry()) {
        std::fprintf(stdout, "RUN %s\n", kv.first.c_str());
        kv.second();
    }
    std::fprintf(stdout, "=== result: %d run, %d failed ===\n", ran(), failures());
    return failures();
}

}} // namespace aim::test

// 注册一个测试用例
#define AIM_TEST(name)                                                       \
    static void name();                                                     \
    static struct name##_reg_t { name##_reg_t() {                           \
        ::aim::test::register_case(#name, name);                            \
    } } name##_reg_inst;                                                    \
    static void name()

#define AIM_EXPECT_TRUE(cond)                                                \
    ::aim::test::record(std::string(__func__) + "#" + std::to_string(__LINE__), \
                        static_cast<bool>(cond))

#define AIM_EXPECT_FALSE(cond)                                               \
    ::aim::test::record(std::string(__func__) + "#" + std::to_string(__LINE__), \
                        !static_cast<bool>(cond))

#define AIM_EXPECT_EQ(a, b)                                                  \
    do {                                                                     \
        auto _a = (a); auto _b = (b);                                        \
        bool _ok = (_a == _b);                                               \
        ::aim::test::record(std::string(__func__) + "#" + std::to_string(__LINE__), \
                            _ok, _ok ? "" : (::aim::test::to_str(_a) + " != " + ::aim::test::to_str(_b))); \
    } while (0)

#define AIM_EXPECT_NEAR(a, b, eps)                                           \
    do {                                                                     \
        auto _a = (a); auto _b = (b);                                        \
        bool _ok = (std::fabs(static_cast<double>(_a) - static_cast<double>(_b)) <= (eps)); \
        ::aim::test::record(std::string(__func__) + "#" + std::to_string(__LINE__), \
                            _ok, _ok ? "" : (::aim::test::to_str(_a) + " ~= " + ::aim::test::to_str(_b))); \
    } while (0)

#define AIM_EXPECT_STREQ(a, b) AIM_EXPECT_EQ(std::string(a), std::string(b))
