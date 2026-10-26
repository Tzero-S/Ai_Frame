#include "aim_test.h"
#include "core/config.h"
#include "core/settings_builder.h"

using namespace aim;

// 默认配置 → 运行时装配：通道名、阈值、范围、FOV、M9 开关全部按 §13.2 落位
AIM_TEST(settings_defaults_mapping) {
    const Config cfg = ConfigManager::Defaults();
    const BuiltSettings b = BuildSettings(cfg, 1920, 1080);

    AIM_EXPECT_STREQ(b.capture_name, "dxgi");
    AIM_EXPECT_STREQ(b.backend_name, "dml");
    AIM_EXPECT_STREQ(b.actuator_name, "sendinput");

    AIM_EXPECT_NEAR(b.pipeline.screen_w, 1920.f, 1e-3f);
    AIM_EXPECT_NEAR(b.pipeline.screen_h, 1080.f, 1e-3f);
    AIM_EXPECT_EQ(b.pipeline.imgsz, 640);
    AIM_EXPECT_NEAR(b.pipeline.conf_threshold, 0.35f, 1e-5f);
    AIM_EXPECT_NEAR(b.pipeline.iou_threshold, 0.45f, 1e-5f);
    // v4：默认不指定类别名 → 装配层无从解析，白名单为空（= 不过滤），
    // 真正的类别解析发生在加载模型之后（RunLoop::Init / preview）
    AIM_EXPECT_EQ(b.pipeline.class_whitelist.size(), 0u);

    // 瞄准范围（§13.2：矩形 320×240）
    AIM_EXPECT_EQ(b.pipeline.scope.mode, ScopeMode::Rect);
    AIM_EXPECT_NEAR(b.pipeline.scope.half_w, 320.f, 1e-3f);
    AIM_EXPECT_NEAR(b.pipeline.scope.half_h, 240.f, 1e-3f);

    // FOV：16:9 自动 → 103° / ≈70.5°
    AIM_EXPECT_NEAR(b.pipeline.fov.hfov_deg, 103.f, 0.2f);
    AIM_EXPECT_NEAR(b.pipeline.fov.vfov_deg, 70.5f, 0.3f);

    // 执行
    AIM_EXPECT_FALSE(b.pipeline.bezier); // kmbox.bezier.enabled 默认关
    AIM_EXPECT_FALSE(b.pipeline.armed);  // 由热键驱动

    // M9
    AIM_EXPECT_TRUE(b.pipeline.behavior_shaping);
    AIM_EXPECT_TRUE(b.pipeline.switch_hysteresis);
    AIM_EXPECT_TRUE(b.pipeline.focus_pause); // hide_on_focus_loss 默认 true
    AIM_EXPECT_TRUE(b.zero_tensors);
    AIM_EXPECT_TRUE(b.neutral_thread_names);

    AIM_EXPECT_EQ(b.target_fps, 120);
    AIM_EXPECT_EQ(b.capture_spec.output_monitor, 0);
}

// 采集 ROI：center_square 取 edge，full 取整屏
AIM_TEST(settings_roi_modes) {
    Config cfg = ConfigManager::Defaults();

    cfg.capture.roi.mode = RoiMode::CenterSquare;
    cfg.capture.roi.edge = 640;
    BuiltSettings a = BuildSettings(cfg, 2560, 1600);
    AIM_EXPECT_EQ(a.capture_spec.roi_w, 640);
    AIM_EXPECT_EQ(a.capture_spec.roi_h, 640);
    AIM_EXPECT_EQ(a.pipeline.capture_spec.roi_w, 640); // 与 Pipeline 保持一致

    cfg.capture.roi.mode = RoiMode::Full;
    BuiltSettings b = BuildSettings(cfg, 2560, 1600);
    AIM_EXPECT_EQ(b.capture_spec.roi_w, 2560);
    AIM_EXPECT_EQ(b.capture_spec.roi_h, 1600);
}

// M9：level=Off 关闭行为整形与迟滞，但"失焦即停"由独立开关控制
AIM_TEST(settings_antidetect_off_keeps_focus_pause) {
    Config cfg = ConfigManager::Defaults();
    cfg.anti_detect.level = AntiDetectLevel::Off;

    const BuiltSettings b = BuildSettings(cfg, 1920, 1080);
    AIM_EXPECT_FALSE(b.pipeline.behavior_shaping);
    AIM_EXPECT_FALSE(b.pipeline.switch_hysteresis);
    AIM_EXPECT_TRUE(b.pipeline.focus_pause); // hide_on_focus_loss 仍为 true

    cfg.anti_detect.hide_on_focus_loss = false;
    const BuiltSettings c = BuildSettings(cfg, 1920, 1080);
    AIM_EXPECT_FALSE(c.pipeline.focus_pause);
}

// 执行通道与贝塞尔：KMBox + bezier.enabled → pipeline.bezier
AIM_TEST(settings_actuator_and_bezier) {
    Config cfg = ConfigManager::Defaults();
    cfg.actuator.channel = ActuatorChannel::Kmbox;
    cfg.actuator.kmbox.bezier.enabled = true;

    const BuiltSettings b = BuildSettings(cfg, 1920, 1080);
    AIM_EXPECT_STREQ(b.actuator_name, "kmbox");
    AIM_EXPECT_TRUE(b.pipeline.bezier);
}

// FOV 手动覆盖优先于自动查表
AIM_TEST(settings_fov_manual_override) {
    Config cfg = ConfigManager::Defaults();
    cfg.fov.manual_override = true;
    cfg.fov.hfov_deg = 90.f;
    cfg.fov.vfov_deg = 59.f;

    const BuiltSettings b = BuildSettings(cfg, 1920, 1080);
    AIM_EXPECT_NEAR(b.pipeline.fov.hfov_deg, 90.f, 1e-4f);
    AIM_EXPECT_NEAR(b.pipeline.fov.vfov_deg, 59.f, 1e-4f);
}

// 边界：非法分辨率/边长不得产生 0 尺寸采集或非法 imgsz
AIM_TEST(settings_guard_invalid_inputs) {
    Config cfg = ConfigManager::Defaults();
    cfg.capture.roi.edge = 0;
    cfg.inference.imgsz = 0;

    const BuiltSettings b = BuildSettings(cfg, 0, 0);
    AIM_EXPECT_NEAR(b.pipeline.screen_w, 1920.f, 1e-3f); // 回落 1920×1080
    AIM_EXPECT_NEAR(b.pipeline.screen_h, 1080.f, 1e-3f);
    AIM_EXPECT_TRUE(b.capture_spec.roi_w >= 1);
    AIM_EXPECT_TRUE(b.capture_spec.roi_h >= 1);
    AIM_EXPECT_TRUE(b.pipeline.imgsz >= 32);
}

int main() {
    return aim::test::run_all();
}

// v4：配置里已有模型类别名时，装配层按名解析成索引（不再靠索引硬编码）
AIM_TEST(settings_resolve_classes_by_name) {
    Config cfg = ConfigManager::Defaults();
    cfg.inference.class_names = {"body", "head", "teammate"};
    cfg.inference.selected_class_names = {"head"};
    const BuiltSettings b = BuildSettings(cfg, 1920, 1080);
    AIM_EXPECT_EQ(b.pipeline.class_whitelist.size(), 1u);
    if (!b.pipeline.class_whitelist.empty()) AIM_EXPECT_EQ(b.pipeline.class_whitelist[0], 1);
    AIM_EXPECT_EQ(b.pipeline.class_names.size(), 3u);
}

// v4：名字对不上时回退自动挑选（body/head 命中，teammate 排除）
AIM_TEST(settings_auto_pick_when_names_miss) {
    Config cfg = ConfigManager::Defaults();
    cfg.inference.class_names = {"body", "head", "teammate", "downed"};
    cfg.inference.selected_class_names = {"不存在的类"};
    const BuiltSettings b = BuildSettings(cfg, 1920, 1080);
    AIM_EXPECT_EQ(b.pipeline.class_whitelist.size(), 2u);
    if (b.pipeline.class_whitelist.size() == 2) {
        AIM_EXPECT_EQ(b.pipeline.class_whitelist[0], 0);
        AIM_EXPECT_EQ(b.pipeline.class_whitelist[1], 1);
    }
}
