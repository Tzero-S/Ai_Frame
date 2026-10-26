#include "aim_test.h"
#include "core/config_store.h"
#include "core/json.h"

#include <filesystem>
#include <string>

using namespace aim;

AIM_TEST(config_store_defaults) {
    ConfigStore s;
    AIM_EXPECT_EQ(s.Version(), 0);
    AIM_EXPECT_EQ(s.Get().schema_version, 4);
    AIM_EXPECT_EQ(s.Get().inference.target_fps, 120);
}

// Apply = 整份替换：未提交的分组回落默认值（§5.1.3 原始语义，供"重置为默认"场景）
AIM_TEST(config_store_apply_replace_semantics) {
    ConfigStore s;
    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(R"({"aim":{"scope":{"half_w":400}}})", v, e));

    const auto r = s.Apply(v);
    AIM_EXPECT_TRUE(r.ok());
    AIM_EXPECT_EQ(r.value(), 1);
    AIM_EXPECT_EQ(s.Get().aim.scope.half_w, 400);
    AIM_EXPECT_EQ(s.Get().capture.mode, CaptureMode::Dxgi); // 缺失字段补默认
}

// ApplyMerge = 深合并：未提交的分组/字段保留现值（控制台只渲染部分分组时的正确语义）
AIM_TEST(config_store_merge_preserves_absent_sections) {
    ConfigStore s;

    // 先写入非默认 anti_detect（模拟用户已关闭防检测）
    json::Value seed;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(
        R"({"anti_detect":{"level":"off","jitter_px":0.0,"neutral_thread_names":false}})", seed, e));
    AIM_EXPECT_TRUE(s.ApplyMerge(seed).ok());
    AIM_EXPECT_EQ(s.Get().anti_detect.level, AntiDetectLevel::Off);

    // 再提交一个"只带 aim"的 patch（控制台点「应用配置」的实际形态）
    json::Value patch;
    AIM_EXPECT_TRUE(json::parse(R"({"aim":{"scope":{"half_w":400}}})", patch, e));
    const auto r = s.ApplyMerge(patch);
    AIM_EXPECT_TRUE(r.ok());
    AIM_EXPECT_EQ(r.value(), 2);

    AIM_EXPECT_EQ(s.Get().aim.scope.half_w, 400);
    // 关键：anti_detect 不得被重置
    AIM_EXPECT_EQ(s.Get().anti_detect.level, AntiDetectLevel::Off);
    AIM_EXPECT_NEAR(s.Get().anti_detect.jitter_px, 0.f, 1e-6f);
    AIM_EXPECT_FALSE(s.Get().anti_detect.neutral_thread_names);
}

// 同组内未提交的字段同样保留（如 inference 只改 target_fps 时 imgsz/conf 不变）
AIM_TEST(config_store_merge_preserves_sibling_fields) {
    ConfigStore s;
    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(R"({"inference":{"target_fps":240}})", v, e));

    AIM_EXPECT_TRUE(s.ApplyMerge(v).ok());
    AIM_EXPECT_EQ(s.Get().inference.target_fps, 240);
    AIM_EXPECT_EQ(s.Get().inference.imgsz, 640);
    AIM_EXPECT_NEAR(s.Get().inference.conf_threshold, 0.35f, 1e-5f);
    AIM_EXPECT_EQ(s.Get().capture.roi.edge, 640);
}

// 合并结果仍需通过校验；非法值不得落库
AIM_TEST(config_store_merge_invalid_rejected) {
    ConfigStore s;
    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(R"({"inference":{"conf_threshold":1.5}})", v, e));

    const auto r = s.ApplyMerge(v);
    AIM_EXPECT_FALSE(r.ok());
    AIM_EXPECT_EQ(s.Version(), 0);
    AIM_EXPECT_NEAR(s.Get().inference.conf_threshold, 0.35f, 1e-5f);
}

// patch 必须是对象（数组/标量直接拒绝，避免把整份配置替换成非法结构）
AIM_TEST(config_store_merge_requires_object) {
    ConfigStore s;
    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(R"([1,2,3])", v, e));

    const auto r = s.ApplyMerge(v);
    AIM_EXPECT_FALSE(r.ok());
    AIM_EXPECT_EQ(s.Version(), 0);
    AIM_EXPECT_EQ(s.Get().schema_version, 4);
}

AIM_TEST(config_store_apply_invalid_rejected) {
    ConfigStore s;
    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(R"({"inference":{"conf_threshold":1.5}})", v, e));

    const auto r = s.Apply(v);
    AIM_EXPECT_FALSE(r.ok());
    AIM_EXPECT_EQ(s.Version(), 0); // 版本不变
    AIM_EXPECT_NEAR(s.Get().inference.conf_threshold, 0.35f, 1e-5f);
}

AIM_TEST(config_store_apply_full_and_version) {
    ConfigStore s;
    Config d = ConfigManager::Defaults();
    d.inference.target_fps = 240;
    const std::string js = ConfigManager::ToJson(d, 0);

    const auto r = s.ApplyJson(js);
    AIM_EXPECT_TRUE(r.ok());
    AIM_EXPECT_EQ(r.value(), 1);
    AIM_EXPECT_EQ(s.Get().inference.target_fps, 240);

    const auto r2 = s.ApplyJson(js);
    AIM_EXPECT_TRUE(r2.ok());
    AIM_EXPECT_EQ(r2.value(), 2);
}

AIM_TEST(config_store_save_load_atomic) {
    const std::string path =
        (std::filesystem::temp_directory_path() / "aim_config_store_test.json").string();
    std::filesystem::remove(path);

    ConfigStore a;
    Config d = ConfigManager::Defaults();
    d.weapon.recoil.strength = 12;

    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(ConfigManager::ToJson(d, 0), v, e));
    AIM_EXPECT_TRUE(a.Apply(v).ok());
    AIM_EXPECT_TRUE(a.SaveToFile(path).ok());
    AIM_EXPECT_TRUE(std::filesystem::exists(path));

    ConfigStore b;
    AIM_EXPECT_TRUE(b.LoadFromFile(path).ok());
    AIM_EXPECT_EQ(b.Version(), 1);
    AIM_EXPECT_EQ(b.Get().weapon.recoil.strength, 12);

    std::filesystem::remove(path);
}

int main() {
    return aim::test::run_all();
}
