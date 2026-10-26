#include "aim_test.h"
#include "core/config.h"
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace aim;

AIM_TEST(config_defaults_match_spec) {
    Config d = ConfigManager::Defaults();
    AIM_EXPECT_EQ(d.schema_version, 4);
    AIM_EXPECT_EQ(d.capture.mode, CaptureMode::Dxgi);
    AIM_EXPECT_EQ(d.capture.roi.edge, 640);
    AIM_EXPECT_EQ(d.inference.backend, Backend::Dml);
    AIM_EXPECT_EQ(d.inference.target_fps, 120);
    AIM_EXPECT_NEAR(d.inference.conf_threshold, 0.35f, 1e-5f);
    AIM_EXPECT_NEAR(d.inference.iou_threshold, 0.45f, 1e-5f);
    AIM_EXPECT_STREQ(d.aim.hotkey_primary.c_str(), "MOUSE2");
    AIM_EXPECT_STREQ(d.aim.hotkey_secondary.c_str(), "MOUSE4");
    AIM_EXPECT_EQ(d.aim.activation_mode, ActivationMode::Hold);
    AIM_EXPECT_EQ(d.aim.scope.half_w, 320);
    AIM_EXPECT_EQ(d.actuator.channel, ActuatorChannel::SendInput);
    AIM_EXPECT_STREQ(d.actuator.kmbox.ip.c_str(), "192.168.2.188");
    AIM_EXPECT_EQ(d.actuator.kmbox.port, 8088);
    AIM_EXPECT_STREQ(d.actuator.kmbox.uid.c_str(), "529C5C53");
    AIM_EXPECT_EQ(d.weapon.recoil.strength, 5);
    AIM_EXPECT_EQ(d.weapon.recoil.rpm, 600);
    AIM_EXPECT_EQ(d.weapon.trigger.rifle_delay_ms, 70);
    AIM_EXPECT_NEAR(d.fov.hfov_deg, 103.0f, 1e-4f);  // §13.2 / §5.8.2
    AIM_EXPECT_NEAR(d.fov.vfov_deg, 59.0f, 1e-4f);
    AIM_EXPECT_STREQ(d.hotkeys.toggle_hud.c_str(), "F9");
    AIM_EXPECT_STREQ(d.hotkeys.toggle_aim.c_str(), "F4");
}

AIM_TEST(config_roundtrip_json) {
    Config d = ConfigManager::Defaults();
    d.inference.target_fps = 240;
    d.inference.threads = 8;
    d.inference.async_capture = false;
    d.weapon.recoil.strength = 12.5f;
    d.weapon.recoil.strength_ads = 22.5f;
    d.weapon.recoil.base_px = 4500.f;
    d.aim.micro_adjust.up = 12;
    d.actuator.channel = ActuatorChannel::Kmbox;
    d.inference.selected_class_names = {"head", "body"};
    d.inference.class_names = {"head", "body"};
    std::string js = ConfigManager::ToJson(d, 2);

    auto r = ConfigManager::FromJson(js);
    AIM_EXPECT_TRUE(r.ok());
    if (!r.ok()) return; // 避免在失败分支上取 value() 抛异常
    const Config& c = r.value();
    AIM_EXPECT_EQ(c.schema_version, 4);
    AIM_EXPECT_EQ(c.inference.target_fps, 240);
    AIM_EXPECT_EQ(c.inference.threads, 8);
    AIM_EXPECT_FALSE(c.inference.async_capture);
    AIM_EXPECT_NEAR(c.weapon.recoil.strength, 12.5f, 1e-4f);
    AIM_EXPECT_NEAR(c.weapon.recoil.strength_ads, 22.5f, 1e-4f);
    AIM_EXPECT_NEAR(c.weapon.recoil.base_px, 4500.f, 1e-3f);
    AIM_EXPECT_EQ(c.aim.micro_adjust.up, 12);
    AIM_EXPECT_EQ(c.actuator.channel, ActuatorChannel::Kmbox);
    // 数组往返（曾因未设 Array 类型而丢字段）；v4 存名字而非索引
    AIM_EXPECT_EQ(c.inference.selected_class_names.size(), 2u);
    AIM_EXPECT_STREQ(c.inference.selected_class_names[1].c_str(), "body");
    AIM_EXPECT_EQ(c.inference.class_names.size(), 2u);
    AIM_EXPECT_STREQ(c.inference.class_names[0].c_str(), "head");
    // 未改字段仍保持默认
    AIM_EXPECT_EQ(c.weapon.recoil.rpm, 600); // 未改字段仍保持默认
    AIM_EXPECT_STREQ(c.aim.hotkey_primary.c_str(), "MOUSE2");
}

AIM_TEST(config_migration_fills_missing) {
    // 模拟一个极简（v1 风格）配置，缺大量字段
    std::string minimal = R"({"capture":{"mode":"gdi"},"actuator":{"channel":"ghub"}})";
    auto r = ConfigManager::FromJson(minimal);
    AIM_EXPECT_TRUE(r.ok());
    if (!r.ok()) return;
    const Config& c = r.value();
    AIM_EXPECT_EQ(c.schema_version, 4);                 // 迁移后统一为 3
    AIM_EXPECT_EQ(c.capture.mode, CaptureMode::Gdi);    // 显式值保留
    // 缺失字段被默认值补齐
    AIM_EXPECT_EQ(c.inference.target_fps, 120);
    AIM_EXPECT_EQ(c.actuator.channel, ActuatorChannel::Ghub);
    AIM_EXPECT_EQ(c.aim.scope.half_w, 320);
    AIM_EXPECT_STREQ(c.actuator.kmbox.ip.c_str(), "192.168.2.188");
}

AIM_TEST(config_save_load_file) {
    // 与工作目录无关的临时路径（CTest 与脚本两种运行方式都可用）
    const std::string path =
        (std::filesystem::temp_directory_path() / "aim_config_test.json").string();
    Config d = ConfigManager::Defaults();
    d.weapon.recoil.strength = 11;
    auto s = ConfigManager::SaveToFile(path, d);
    AIM_EXPECT_TRUE(s.ok());
    if (!s.ok()) return;

    auto r = ConfigManager::LoadFromFile(path);
    AIM_EXPECT_TRUE(r.ok());
    if (!r.ok()) return;
    AIM_EXPECT_EQ(r.value().weapon.recoil.strength, 11);
    AIM_EXPECT_EQ(r.value().schema_version, 4);
    std::remove(path.c_str());
}

AIM_TEST(config_validate_rejects_bad_conf) {
    Config d = ConfigManager::Defaults();
    d.inference.conf_threshold = 1.5f; // 超出 [0.01,0.99]
    auto v = ConfigManager::Validate(d);
    AIM_EXPECT_FALSE(v.ok());
}

AIM_TEST(config_enum_roundtrip) {
    Backend b;
    AIM_EXPECT_TRUE(from_string("cpu", b));
    AIM_EXPECT_EQ(b, Backend::Cpu);
    AIM_EXPECT_STREQ(to_string(Backend::Tensorrt), "tensorrt");

    ActuatorChannel ch;
    AIM_EXPECT_TRUE(from_string("kmbox", ch));
    AIM_EXPECT_EQ(ch, ActuatorChannel::Kmbox);
    AIM_EXPECT_FALSE(from_string("nope", ch));

    ScopeMode sm;
    AIM_EXPECT_TRUE(from_string("fov", sm));
    AIM_EXPECT_EQ(sm, ScopeMode::Fov);
}

int main() {
    return aim::test::run_all();
}

AIM_TEST(config_migrate_v3_index_to_v4_names) {
    // v3 配置：selected_classes 是索引，class_names 是占位默认值
    const char* v3 = R"({
      "schema_version": 3,
      "inference": {
        "class_names": ["head", "body"],
        "selected_classes": [1]
      }
    })";
    auto r = ConfigManager::FromJson(v3);
    AIM_EXPECT_TRUE(r.ok());
    if (!r.ok()) return;
    const Config& c = r.value();
    AIM_EXPECT_EQ(c.schema_version, 4);
    // 索引 1 → 名字 "body"
    AIM_EXPECT_EQ(c.inference.selected_class_names.size(), 1u);
    AIM_EXPECT_STREQ(c.inference.selected_class_names[0].c_str(), "body");
}

AIM_TEST(config_v4_empty_selection_means_auto) {
    // 默认配置：selected_class_names 为空 = 加载模型时自动挑选
    const Config d = ConfigManager::Defaults();
    AIM_EXPECT_EQ(d.inference.selected_class_names.size(), 0u);
}
