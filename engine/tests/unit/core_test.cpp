#include "aim_test.h"
#include "core/types.h"
#include "core/result.h"
#include "core/json.h"
#include "core/registry.h"
#include "core/logger.h"

#include <memory>
#include <cmath>
#include <limits>

using namespace aim;

// ---------------- types ----------------
AIM_TEST(types_rect_geometry) {
    RectF r{10.f, 20.f, 100.f, 50.f};
    AIM_EXPECT_NEAR(r.cx(), 60.f, 1e-4f);
    AIM_EXPECT_NEAR(r.cy(), 45.f, 1e-4f);
    AIM_EXPECT_NEAR(r.right(), 110.f, 1e-4f);
    AIM_EXPECT_NEAR(r.bottom(), 70.f, 1e-4f);
}

AIM_TEST(types_detection_aim_point) {
    Detection d;
    d.box = RectF{0.f, 0.f, 40.f, 80.f};
    PointF p = d.aim_point();
    AIM_EXPECT_NEAR(p.x, 20.f, 1e-4f);
    AIM_EXPECT_NEAR(p.y, 40.f, 1e-4f);
}

// ---------------- result ----------------
AIM_TEST(result_ok_fail) {
    auto ok = Result<int>::Ok(42);
    AIM_EXPECT_TRUE(ok.ok());
    AIM_EXPECT_EQ(ok.value(), 42);

    auto fail = Result<int>::Fail(7, "boom", "reinstall driver");
    AIM_EXPECT_FALSE(fail.ok());
    AIM_EXPECT_EQ(fail.error().code, 7);
    AIM_EXPECT_STREQ(fail.error().message, "boom");
    AIM_EXPECT_STREQ(fail.error().advice, "reinstall driver");
}

// ---------------- json ----------------
AIM_TEST(json_parse_scalars) {
    json::Value v;
    std::string err;
    bool ok = json::parse(R"({"a":1,"b":-2.5,"c":true,"d":"hi","e":null})", v, err);
    AIM_EXPECT_TRUE(ok);
    AIM_EXPECT_EQ(err.empty(), true);
    AIM_EXPECT_EQ(v["a"].asInt(), 1);
    AIM_EXPECT_NEAR(v["b"].asNumber(), -2.5, 1e-9);
    AIM_EXPECT_EQ(v["c"].asBool(), true);
    AIM_EXPECT_STREQ(v["d"].asString(), "hi");
    AIM_EXPECT_TRUE(v["e"].isNull());
}

AIM_TEST(json_parse_nested_and_array) {
    json::Value v;
    std::string err;
    bool ok = json::parse(R"({"obj":{"x":[1,2,3],"y":"z"}})", v, err);
    AIM_EXPECT_TRUE(ok);
    const json::Value* x = v.find("obj")->find("x");
    AIM_EXPECT_EQ(x->size(), 3u);
    AIM_EXPECT_EQ((*x)[0].asInt(), 1);
    AIM_EXPECT_EQ((*x)[1].asInt(), 2);
    AIM_EXPECT_STREQ(v["obj"]["y"].asString(), "z");
}

AIM_TEST(json_escapes_and_unicode) {
    json::Value v;
    std::string err;
    bool ok = json::parse(R"("line1\nline2\t\"q\"\u4e2d")", v, err);
    AIM_EXPECT_TRUE(ok);
    // 反序列化后：line1<LF>line2<TAB>"q"中
    AIM_EXPECT_EQ(v.str.find('\n') != std::string::npos, true);
    AIM_EXPECT_EQ(v.str.find('\t') != std::string::npos, true);
    AIM_EXPECT_EQ(v.str.find("中") != std::string::npos, true);
}

AIM_TEST(json_roundtrip_dump) {
    json::Value v;
    std::string err;
    json::parse(R"({"schema_version":3,"name":"default"})", v, err);
    std::string compact = json::dump(v, 0);
    // 重新解析紧凑结果，确认等价
    json::Value v2;
    std::string err2;
    bool ok = json::parse(compact, v2, err2);
    AIM_EXPECT_TRUE(ok);
    AIM_EXPECT_EQ(v2["schema_version"].asInt(), 3);
    AIM_EXPECT_STREQ(v2["name"].asString(), "default");
}

// 浮点序列化：float 字段应输出可读短表示（0.35 而非 0.349999994），
// 且必须能按 float32 精度无损往返。
AIM_TEST(json_float_shortest_representation) {
    json::Value v;
    v["conf"] = 0.35;
    v["jitter"] = static_cast<double>(0.8f);
    v["ratio"] = static_cast<double>(0.1f);
    v["offset"] = static_cast<double>(0.12f);
    const std::string s = json::dump(v, 0);

    AIM_EXPECT_TRUE(s.find("0.349999") == std::string::npos);
    AIM_EXPECT_TRUE(s.find("0.800000") == std::string::npos);
    AIM_EXPECT_TRUE(s.find("0.100000") == std::string::npos);
    AIM_EXPECT_TRUE(s.find("0.119999") == std::string::npos);
    AIM_EXPECT_TRUE(s.find("0.35") != std::string::npos);

    json::Value back;
    std::string err;
    AIM_EXPECT_TRUE(json::parse(s, back, err));
    AIM_EXPECT_NEAR(static_cast<float>(back["conf"].asNumber()), 0.35f, 1e-6f);
    AIM_EXPECT_NEAR(static_cast<float>(back["jitter"].asNumber()), 0.8f, 1e-6f);
    AIM_EXPECT_NEAR(static_cast<float>(back["ratio"].asNumber()), 0.1f, 1e-6f);
    AIM_EXPECT_NEAR(static_cast<float>(back["offset"].asNumber()), 0.12f, 1e-6f);
}

// 非有限值不得输出 NaN/Inf（非法 JSON），按 JSON.stringify 惯例降级为 null
AIM_TEST(json_non_finite_numbers_dump_null) {
    json::Value v;
    v["a"] = std::nan("");
    v["b"] = std::numeric_limits<double>::infinity();
    const std::string s = json::dump(v, 0);
    AIM_EXPECT_TRUE(s.find("nan") == std::string::npos);
    AIM_EXPECT_TRUE(s.find("inf") == std::string::npos);
    AIM_EXPECT_TRUE(s.find("null") != std::string::npos);

    json::Value back;
    std::string err;
    AIM_EXPECT_TRUE(json::parse(s, back, err)); // 仍是合法 JSON
    AIM_EXPECT_TRUE(back["a"].isNull());
    AIM_EXPECT_TRUE(back["b"].isNull());
}

AIM_TEST(json_invalid_rejected) {
    json::Value v;
    std::string err;
    bool ok = json::parse(R"({"a":1,)", v, err); // 截断
    AIM_EXPECT_FALSE(ok);
    AIM_EXPECT_EQ(err.empty(), false);
}

// P1-1 回归：嵌套深度必须被拒绝，而不是打爆线程栈。
// 实测修复前 10000 层（20KB 输入）会以 STATUS_STACK_OVERFLOW 直接终止进程。
AIM_TEST(json_rejects_deep_nesting) {
    const int depth = 10000;
    std::string deep(depth, '[');
    deep.append(depth, ']');

    json::Value v;
    std::string err;
    const bool ok = json::parse(deep, v, err);
    AIM_EXPECT_FALSE(ok);
    AIM_EXPECT_TRUE(err.find("nesting too deep") != std::string::npos);

    // 合理深度仍然可用（配置/报文实际不超过 10 层）
    std::string shallow(100, '[');
    shallow.append(100, ']');
    json::Value v2;
    std::string err2;
    AIM_EXPECT_TRUE(json::parse(shallow, v2, err2));
}

// P3-3 回归：非法数字必须在解析期被拒绝，不能被 strtod 静默截断
AIM_TEST(json_rejects_malformed_numbers) {
    const char* bad[] = {
        R"({"a":1-2})",   R"({"a":1e})",    R"({"a":1e+})",  R"({"a":--1})",
        R"({"a":01})",    R"({"a":1.})",    R"({"a":.5})",   R"({"a":+1})",
        R"({"a":1e++5})", R"({"a":1.2.3})",
    };
    for (const char* text : bad) {
        json::Value v;
        std::string err;
        const bool ok = json::parse(text, v, err);
        AIM_EXPECT_FALSE(ok);
    }

    // 合法形式全部通过，且数值正确
    struct Case { const char* text; double want; };
    const Case good[] = {
        {R"({"a":0})", 0.0},      {R"({"a":-0})", 0.0},      {R"({"a":12})", 12.0},
        {R"({"a":-3.5})", -3.5},  {R"({"a":1e3})", 1000.0},  {R"({"a":1.5E-2})", 0.015},
        {R"({"a":0.25})", 0.25},
    };
    for (const auto& c : good) {
        json::Value v;
        std::string err;
        AIM_EXPECT_TRUE(json::parse(c.text, v, err));
        AIM_EXPECT_NEAR(v["a"].asNumber(-1.0), c.want, 1e-9);
    }
}

// P3-3 回归：UTF-16 代理对必须合并成一个 4 字节 UTF-8 码点（旧实现产出 CESU-8 乱码）
AIM_TEST(json_surrogate_pair_to_utf8) {
    json::Value v;
    std::string err;
    // U+1F600 = 😀 = F0 9F 98 80
    AIM_EXPECT_TRUE(json::parse(R"({"s":"\uD83D\uDE00"})", v, err));
    const std::string got = v["s"].asString();
    AIM_EXPECT_EQ(got.size(), static_cast<size_t>(4));
    AIM_EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(got[0])), 0xF0);
    AIM_EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(got[1])), 0x9F);
    AIM_EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(got[2])), 0x98);
    AIM_EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(got[3])), 0x80);

    // 孤立代理 → 替换字符 U+FFFD（EF BF BD），不产生非法 UTF-8
    AIM_EXPECT_TRUE(json::parse(R"({"s":"\uD83D"})", v, err));
    const std::string lone = v["s"].asString();
    AIM_EXPECT_EQ(lone.size(), static_cast<size_t>(3));
    AIM_EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(lone[0])), 0xEF);

    // BMP 内的中文仍正常：U+4E2D = E4 B8 AD
    AIM_EXPECT_TRUE(json::parse(R"({"s":"\u4E2D"})", v, err));
    AIM_EXPECT_EQ(v["s"].asString(), std::string("\xE4\xB8\xAD"));
}

// ---------------- registry (SPI) ----------------
struct DummySpi { virtual ~DummySpi() = default; virtual int id() const = 0; };
struct DumA : DummySpi { int id() const override { return 1; } };
struct DumB : DummySpi { int id() const override { return 2; } };

AIM_TEST(registry_create_and_missing) {
    Registry::register_factory<DummySpi>("a", [] { return std::make_unique<DumA>(); });
    Registry::register_factory<DummySpi>("b", [] { return std::make_unique<DumB>(); });

    auto pa = Registry::create<DummySpi>("a");
    AIM_EXPECT_TRUE(pa != nullptr);
    AIM_EXPECT_EQ(pa->id(), 1);

    auto pb = Registry::create<DummySpi>("b");
    AIM_EXPECT_EQ(pb->id(), 2);

    auto pm = Registry::create<DummySpi>("missing");
    AIM_EXPECT_TRUE(pm == nullptr);

    AIM_EXPECT_EQ(Registry::has<DummySpi>("a"), true);
    AIM_EXPECT_EQ(Registry::has<DummySpi>("zzz"), false);
}

// ---------------- logger ----------------
AIM_TEST(logger_runs_without_throw) {
    auto& lg = Logger::instance();
    lg.set_level(LogLevel::Trace);
    lg.set_console(false);        // 避免污染测试输出
    lg.set_directory("");         // 关闭文件落盘（仅验证不抛异常）
    lg.engine(LogLevel::Info, "logger self-test info");
    lg.engine(LogLevel::Error, "logger self-test error");
    lg.perf("perf line");
    lg.audit("audit line");
    lg.crash("crash line");
    // 不写文件时 file_path 应为空
    AIM_EXPECT_EQ(lg.file_path(LogChannel::Engine).empty(), true);
}

int main() {
    return aim::test::run_all();
}
