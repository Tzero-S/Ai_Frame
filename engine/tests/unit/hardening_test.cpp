#include "aim_test.h"
#include "core/base64.h"
#include "core/config.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace aim;

namespace {

// 返回校验失败的 code（成功返回 0），便于逐条断言"是哪条规则拦下的"
int Code(const Config& c) {
    const auto r = ConfigManager::Validate(c);
    return r.ok() ? 0 : r.error().code;
}

} // namespace

// 默认配置必须通过全部校验（防止新规则误伤默认值）
AIM_TEST(validate_defaults_pass) {
    AIM_EXPECT_EQ(Code(ConfigManager::Defaults()), 0);
}

// 推理参数
AIM_TEST(validate_rejects_bad_inference) {
    Config c = ConfigManager::Defaults();
    c.inference.imgsz = 16;
    AIM_EXPECT_EQ(Code(c), 1003);

    c = ConfigManager::Defaults();
    c.inference.conf_threshold = std::nanf("");
    AIM_EXPECT_EQ(Code(c), 1004); // NaN 不得绕过区间判断

    c = ConfigManager::Defaults();
    c.inference.iou_threshold = 1.5f;
    AIM_EXPECT_EQ(Code(c), 1005);

    c = ConfigManager::Defaults();
    c.inference.target_fps = 0;
    AIM_EXPECT_EQ(Code(c), 1006);

    c = ConfigManager::Defaults();
    c.inference.threads = 999;
    AIM_EXPECT_EQ(Code(c), 1034);

    c = ConfigManager::Defaults();
    c.inference.threads = 0; // 0 = 自动，合法
    AIM_EXPECT_EQ(Code(c), 0);
}

// 瞄准与平滑
AIM_TEST(validate_rejects_bad_aim) {
    Config c = ConfigManager::Defaults();
    c.aim.crosshair_offset_x = 1e9f;
    AIM_EXPECT_EQ(Code(c), 1011);

    c = ConfigManager::Defaults();
    c.aim.target_point.offset_y_ratio = 1.5f;
    AIM_EXPECT_EQ(Code(c), 1012);

    c = ConfigManager::Defaults();
    c.aim.smoothing.model = "spring";
    AIM_EXPECT_EQ(Code(c), 1014);

    c = ConfigManager::Defaults();
    c.aim.smoothing.alpha = 1.5f;
    AIM_EXPECT_EQ(Code(c), 1015);

    c = ConfigManager::Defaults();
    c.aim.smoothing.omega = 0.f;
    AIM_EXPECT_EQ(Code(c), 1016);

    c = ConfigManager::Defaults();
    c.aim.smoothing.max_jerk = -1.f;
    AIM_EXPECT_EQ(Code(c), 1017);
}

// 视野与开镜表
AIM_TEST(validate_rejects_bad_fov) {
    Config c = ConfigManager::Defaults();
    c.fov.hfov_deg = 0.f;
    AIM_EXPECT_EQ(Code(c), 1018);

    c = ConfigManager::Defaults();
    c.fov.ads_zoom = 0.5f;
    AIM_EXPECT_EQ(Code(c), 1020);

    c = ConfigManager::Defaults();
    c.fov.ads_levels.push_back(FovConfig::AdsLevel{ 0.5f, 90.f });
    AIM_EXPECT_EQ(Code(c), 1021);

    c = ConfigManager::Defaults();
    c.fov.ads_levels.push_back(FovConfig::AdsLevel{ 2.f, 200.f });
    AIM_EXPECT_EQ(Code(c), 1022);

    // 合法表应通过
    c = ConfigManager::Defaults();
    c.fov.ads_levels = { { 1.f, 103.f }, { 2.f, 51.f } };
    c.fov.ads_zoom = 2.f;
    AIM_EXPECT_EQ(Code(c), 0);
}

// 武器与执行
AIM_TEST(validate_rejects_bad_weapon_and_actuator) {
    Config c = ConfigManager::Defaults();
    c.weapon.recoil.strength = 31.f; // 上限 30，越界
    AIM_EXPECT_EQ(Code(c), 1024);

    c = ConfigManager::Defaults();
    c.weapon.recoil.rpm = 0;
    AIM_EXPECT_EQ(Code(c), 1025);

    c = ConfigManager::Defaults();
    c.weapon.trigger.hit_band_ratio = 0.f;
    AIM_EXPECT_EQ(Code(c), 1026);

    c = ConfigManager::Defaults();
    c.weapon.trigger.rifle_delay_ms = 9999;
    AIM_EXPECT_EQ(Code(c), 1027);

    c = ConfigManager::Defaults();
    c.actuator.kmbox.port = 0;
    AIM_EXPECT_EQ(Code(c), 1029);

    c = ConfigManager::Defaults();
    c.weapon.recoil.strength_ads = 31.f;
    AIM_EXPECT_EQ(Code(c), 1038);

    c = ConfigManager::Defaults();
    c.weapon.recoil.strength = 30.5f;   // 超过 30
    AIM_EXPECT_EQ(Code(c), 1024);

    c = ConfigManager::Defaults();
    c.weapon.recoil.base_px = 6001.f;
    AIM_EXPECT_EQ(Code(c), 1037);

    // 上限值必须合法
    c = ConfigManager::Defaults();
    c.weapon.recoil.strength = 30.f;
    c.weapon.recoil.base_px = 6000.f;
    c.weapon.recoil.strength_ads = 30.f;
    AIM_EXPECT_EQ(Code(c), 0);
}

// 采集与反检测
AIM_TEST(validate_rejects_bad_capture_and_opsec) {
    Config c = ConfigManager::Defaults();
    c.capture.roi.edge = 0;
    AIM_EXPECT_EQ(Code(c), 1023);

    c = ConfigManager::Defaults();
    c.anti_detect.idle_fps = 0;
    AIM_EXPECT_EQ(Code(c), 1030);

    c = ConfigManager::Defaults();
    c.anti_detect.jitter_px = 100.f;
    AIM_EXPECT_EQ(Code(c), 1031);

    c = ConfigManager::Defaults();
    c.anti_detect.timing_jitter_ratio = 2.f;
    AIM_EXPECT_EQ(Code(c), 1032);

    c = ConfigManager::Defaults();
    c.anti_detect.switch_hysteresis_frames = 100;
    AIM_EXPECT_EQ(Code(c), 1033);
}

// base64：多长度往返（含长串，覆盖累加器溢出回归）
// P1-5 回归：补齐的校验项必须逐条生效（1039-1051）
AIM_TEST(validate_rejects_newly_added_fields) {
    Config c = ConfigManager::Defaults();

    c = ConfigManager::Defaults();
    c.capture.output_monitor = -1;
    AIM_EXPECT_EQ(Code(c), 1039);
    c = ConfigManager::Defaults();
    c.capture.output_monitor = 16;
    AIM_EXPECT_EQ(Code(c), 1039);

    c = ConfigManager::Defaults();
    c.actuator.sendinput.move_scale = 0.f;
    AIM_EXPECT_EQ(Code(c), 1040);
    c = ConfigManager::Defaults();
    c.actuator.sendinput.move_scale = 101.f;
    AIM_EXPECT_EQ(Code(c), 1040);

    c = ConfigManager::Defaults();
    c.actuator.sendinput.min_step = -1;
    AIM_EXPECT_EQ(Code(c), 1041);

    c = ConfigManager::Defaults();
    c.actuator.ghub.hz = 0;
    AIM_EXPECT_EQ(Code(c), 1042);

    c = ConfigManager::Defaults();
    c.actuator.ghub.max_step = 200;
    AIM_EXPECT_EQ(Code(c), 1043);

    c = ConfigManager::Defaults();
    c.actuator.kmbox.ip = "192.168.2.999";
    AIM_EXPECT_EQ(Code(c), 1044);

    c = ConfigManager::Defaults();
    c.actuator.kmbox.uid = "529C5C5";
    AIM_EXPECT_EQ(Code(c), 1045);

    c = ConfigManager::Defaults();
    c.aim.micro_adjust.step_px = 201;
    AIM_EXPECT_EQ(Code(c), 1046);

    c = ConfigManager::Defaults();
    c.fov.ads_levels.assign(33, FovConfig::AdsLevel{2.f, 51.f});
    AIM_EXPECT_EQ(Code(c), 1047);

    c = ConfigManager::Defaults();
    c.inference.class_names.assign(257, std::string("x"));
    AIM_EXPECT_EQ(Code(c), 1048);

    c = ConfigManager::Defaults();
    c.inference.class_names.assign(1, std::string(129, 'a'));
    AIM_EXPECT_EQ(Code(c), 1049);

    c = ConfigManager::Defaults();
    c.hotkeys.toggle_hud.assign(65, 'K');
    AIM_EXPECT_EQ(Code(c), 1050);

    // 边界值必须放行（0 与上限本身合法）
    c = ConfigManager::Defaults();
    c.capture.output_monitor = 15;
    c.actuator.sendinput.move_scale = 100.f;
    c.actuator.ghub.hz = 8000;
    c.actuator.ghub.max_step = 127;
    c.aim.micro_adjust.step_px = 200;
    c.fov.ads_levels.assign(32, FovConfig::AdsLevel{2.f, 51.f});
    AIM_EXPECT_EQ(Code(c), 0);
}

// P3-5 回归：超大配置文件必须被拒绝，而不是整个读进内存
AIM_TEST(config_load_rejects_oversize_file) {
    const std::string path = "aim_oversize_cfg_test.json";
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << "{\"schema_version\":4,";
        const std::string pad(4 * 1024 * 1024 + 1024, ' ');
        f << pad << "\"pad\":1}";
    }
    const auto r = ConfigManager::LoadFromFile(path);
    AIM_EXPECT_FALSE(r.ok());
    if (!r.ok()) AIM_EXPECT_EQ(r.error().code, 1051);
    std::remove(path.c_str());
}

AIM_TEST(base64_roundtrip_all_lengths) {
    const size_t lens[] = { 0, 1, 2, 3, 4, 7, 64, 100, 1000, 3000 };
    for (size_t n : lens) {
        std::vector<uint8_t> src(n);
        for (size_t i = 0; i < n; ++i) src[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);

        const std::string enc = base64::Encode(src.data(), n);
        std::vector<uint8_t> dec;
        std::string err;
        AIM_EXPECT_TRUE(base64::Decode(enc, dec, &err));
        AIM_EXPECT_TRUE(err.empty());
        AIM_EXPECT_EQ(dec.size(), n);
        if (dec.size() == n && n > 0) {
            AIM_EXPECT_EQ(static_cast<int>(dec[n - 1]), static_cast<int>(src[n - 1]));
        }
    }
}

// base64：非法字符拒绝；空白容忍；空串合法
AIM_TEST(base64_rejects_and_tolerates) {
    std::vector<uint8_t> out;
    std::string err;
    AIM_EXPECT_FALSE(base64::Decode("!!!!", out, &err));
    AIM_EXPECT_FALSE(err.empty());
    AIM_EXPECT_EQ(out.size(), 0u);

    AIM_EXPECT_TRUE(base64::Decode("", out, nullptr));
    AIM_EXPECT_EQ(out.size(), 0u);

    // 换行/空格应被忽略
    std::vector<uint8_t> a, b;
    AIM_EXPECT_TRUE(base64::Decode("aGVsbG8=", a, nullptr));
    AIM_EXPECT_TRUE(base64::Decode("aGVs\nbG8 =", b, nullptr));
    AIM_EXPECT_EQ(a.size(), b.size());
}

int main() {
    return aim::test::run_all();
}
