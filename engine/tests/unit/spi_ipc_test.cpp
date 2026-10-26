#include "aim_test.h"
#include "core/builtins.h"
#include "core/registry.h"
#include "core/spi.h"
#include "core/config.h"
#include "core/config_schema.h"
#include "core/json.h"
#include "ipc/protocol.h"
#include "actuator/actuator_stub.h"
#include "actuator/ghub_actuator.h"
#include "actuator/kmbox_actuator.h"
#include "actuator/serial_actuator.h"

#include <vector>

using namespace aim;

AIM_TEST(spi_builtin_factories_registered) {
    RegisterBuiltinExtensions();

    AIM_EXPECT_TRUE(Registry::has<spi::ICapture>("dxgi"));
    AIM_EXPECT_TRUE(Registry::has<spi::ICapture>("gdi"));
    AIM_EXPECT_TRUE(Registry::has<spi::ICapture>("scrcpy"));
    AIM_EXPECT_TRUE(Registry::create<spi::ICapture>("dxgi") != nullptr);

    AIM_EXPECT_TRUE(Registry::has<spi::IBackend>("dml"));
    AIM_EXPECT_TRUE(Registry::has<spi::IBackend>("cpu"));
    AIM_EXPECT_TRUE(Registry::create<spi::IBackend>("dml") != nullptr);

    // §4.4 注册名必须与 §5.1.2 的 backend 枚举一致（含 P1 预留的 tensorrt）
    AIM_EXPECT_TRUE(Registry::has<spi::IBackend>("tensorrt"));
    auto trt = Registry::create<spi::IBackend>("tensorrt");
    AIM_EXPECT_TRUE(trt != nullptr);
    if (trt) {
        // 预留实现必须"明确失败"而不是静默成功
        std::vector<Detection> out;
        AIM_EXPECT_FALSE(trt->load(spi::ModelSpec{}));
        AIM_EXPECT_FALSE(trt->infer(spi::Frame{}, out));
        AIM_EXPECT_TRUE(out.empty());
    }

    AIM_EXPECT_TRUE(Registry::has<spi::IActuator>("sendinput"));
    AIM_EXPECT_TRUE(Registry::has<spi::IActuator>("ghub"));
    AIM_EXPECT_TRUE(Registry::has<spi::IActuator>("kmbox"));
    AIM_EXPECT_TRUE(Registry::has<spi::IActuator>("serial"));
}

AIM_TEST(spi_actuator_factories) {
    RegisterBuiltinExtensions();

    auto ghub = Registry::create<spi::IActuator>("ghub");
    AIM_EXPECT_TRUE(ghub != nullptr);
    auto* g = dynamic_cast<actuator::GhubActuator*>(ghub.get());
    AIM_EXPECT_TRUE(g != nullptr);
    if (g) AIM_EXPECT_FALSE(g->is_open());

    auto km = Registry::create<spi::IActuator>("kmbox");
    AIM_EXPECT_TRUE(km != nullptr);
    auto* k = dynamic_cast<actuator::KmboxActuator*>(km.get());
    AIM_EXPECT_TRUE(k != nullptr);
    if (k) AIM_EXPECT_EQ(static_cast<long long>(k->packets_sent()), 0LL);

    auto se = Registry::create<spi::IActuator>("serial");
    AIM_EXPECT_TRUE(se != nullptr);
    AIM_EXPECT_TRUE(dynamic_cast<actuator::SerialActuator*>(se.get()) != nullptr);
}

AIM_TEST(ipc_request_roundtrip) {
    ipc::Request req;
    req.id = "req-1024";
    req.cmd = ipc::cmd::kConfigApply;
    req.ts = 1761292800000LL;
    req.payload["applied_version"] = 12;

    std::string text = ipc::Serialize(req);
    ipc::Request parsed;
    std::string err;
    AIM_EXPECT_TRUE(ipc::ParseRequest(text, parsed, &err));
    AIM_EXPECT_STREQ(parsed.id, "req-1024");
    AIM_EXPECT_STREQ(parsed.cmd, "config.apply");
    AIM_EXPECT_EQ(static_cast<long long>(parsed.ts), 1761292800000LL);
    AIM_EXPECT_EQ(parsed.payload["applied_version"].asInt(), 12);
}

AIM_TEST(ipc_response_roundtrip) {
    ipc::Response resp;
    resp.id = "req-1";
    resp.code = 0;
    resp.msg = "ok";
    resp.payload["applied_version"] = 12;

    std::string text = ipc::Serialize(resp);
    ipc::Response parsed;
    AIM_EXPECT_TRUE(ipc::ParseResponse(text, parsed));
    AIM_EXPECT_STREQ(parsed.id, "req-1");
    AIM_EXPECT_EQ(parsed.code, 0);
    AIM_EXPECT_STREQ(parsed.msg, "ok");
    AIM_EXPECT_EQ(parsed.payload["applied_version"].asInt(), 12);
}

AIM_TEST(ipc_parse_rejects_invalid) {
    ipc::Request req;
    std::string err;
    AIM_EXPECT_FALSE(ipc::ParseRequest("{bad json", req, &err));
    AIM_EXPECT_FALSE(ipc::ParseRequest(R"({"cmd":"x"})", req, &err)); // 缺 id
    AIM_EXPECT_FALSE(ipc::ParseRequest(R"({"id":"1"})", req, &err));  // 缺 cmd
}

AIM_TEST(ipc_command_table) {
    AIM_EXPECT_TRUE(ipc::IsKnownCommand("config.apply"));
    AIM_EXPECT_TRUE(ipc::IsKnownCommand("status.snapshot"));
    AIM_EXPECT_TRUE(ipc::IsKnownCommand("auth.login"));
    AIM_EXPECT_FALSE(ipc::IsKnownCommand("nope.cmd"));
}

AIM_TEST(config_schema_default_valid) {
    std::string js = ConfigManager::ToJson(ConfigManager::Defaults(), 0);
    auto r = ConfigSchema::ValidateJson(js);
    AIM_EXPECT_TRUE(r.ok());
}

AIM_TEST(config_schema_rejects_violations) {
    Config d = ConfigManager::Defaults();
    std::string js = ConfigManager::ToJson(d, 0);

    auto mutate = [&](const std::string& path, auto setter) {
        (void)path; // 仅用于调用点可读性标注
        json::Value v;
        std::string e;
        json::parse(js, v, e);
        setter(v);
        return ConfigSchema::ValidateJson(json::dump(v, 0));
    };

    AIM_EXPECT_FALSE(mutate("conf", [](json::Value& v) {
        v["inference"]["conf_threshold"] = 1.5;
    }).ok());

    AIM_EXPECT_FALSE(mutate("uid", [](json::Value& v) {
        v["actuator"]["kmbox"]["uid"] = "XYZ";
    }).ok());

    AIM_EXPECT_FALSE(mutate("ip", [](json::Value& v) {
        v["actuator"]["kmbox"]["ip"] = "999.1.1.1";
    }).ok());

    AIM_EXPECT_FALSE(mutate("edge", [](json::Value& v) {
        v["capture"]["roi"]["edge"] = 999;
    }).ok());

    AIM_EXPECT_FALSE(mutate("backend", [](json::Value& v) {
        v["inference"]["backend"] = "foo";
    }).ok());

    AIM_EXPECT_FALSE(mutate("ver", [](json::Value& v) {
        v["schema_version"] = 2;
    }).ok());

    AIM_EXPECT_FALSE(mutate("missing", [](json::Value& v) {
        v["capture"] = 5; // 非对象 → required 分组缺失
    }).ok());
}

AIM_TEST(ipv4_and_uid_helpers) {
    AIM_EXPECT_TRUE(ConfigSchema::IsIpv4("192.168.2.188"));
    AIM_EXPECT_TRUE(ConfigSchema::IsIpv4("0.0.0.0"));
    AIM_EXPECT_FALSE(ConfigSchema::IsIpv4("256.1.1.1"));
    AIM_EXPECT_FALSE(ConfigSchema::IsIpv4("1.2.3"));
    AIM_EXPECT_FALSE(ConfigSchema::IsIpv4("a.b.c.d"));

    AIM_EXPECT_TRUE(ConfigSchema::IsHex8("529C5C53"));
    AIM_EXPECT_FALSE(ConfigSchema::IsHex8("529C5C5"));
    AIM_EXPECT_FALSE(ConfigSchema::IsHex8("GGGGGGGG"));
}

int main() {
    return aim::test::run_all();
}
