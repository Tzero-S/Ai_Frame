#include "aim_test.h"
#include "fixtures_util.h"
#include "inference/backend_ort.h"
#include "postprocess/nms.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace aim;

namespace {

// 用指定 EP 跑一遍 fixture，返回 NMS 后的检测框。
// 说明：CPU 路径在 MinGW（无 DML）构建下同样可用，因此不加 AIM_ENABLE_DML 守卫；
//       DML 用例在调用处自行跳过。
bool RunFixture(inference::OrtEp ep, const std::string& model, const std::string& image,
                std::vector<Detection>& out, bool zero_outputs = false) {
    inference::OrtBackend be(ep);
    spi::ModelSpec spec;
    spec.path = model;
    spec.imgsz = 960;
    spec.num_classes = 2;
    spec.num_kpts = 0;
    spec.conf_threshold = 0.35f;
    spec.input_nhwc_u8 = false;
    spec.threads = 4;
    spec.zero_outputs = zero_outputs;
    if (!be.load(spec)) return false;

    std::vector<uint8_t> bgra;
    if (!test::LoadPngAsBgra(std::filesystem::path(image).wstring(), 960, 960, bgra)) return false;
    if (bgra.size() != static_cast<size_t>(960) * 960 * 4) return false;

    spi::Frame f;
    f.w = 960;
    f.h = 960;
    f.bgra = bgra.data();
    f.valid = true;

    out.clear();
    if (!be.infer(f, out)) return false;
    Nms(out, 0.45f); // 与 Pipeline 一致
    return true;
}

} // namespace

// VALORANT 最优模型（best_s960_fp16_dml.onnx）真实推理校验：
// 与 Python ORT 结果对齐（Body 0.8374 / Head 0.6406）。
AIM_TEST(real_model_metadata_and_inference) {
    const std::string model = test::ValorantModelPath();
    if (!test::FileExists(model)) return; // 模型缺失则跳过

    const std::string image = test::EnvOr(
        "AIM_VALORANT_IMAGE", test::FixturePath("valorant_sample_960.png"));
    if (!test::FileExists(image)) return;

    inference::OrtBackend be(inference::OrtEp::Cpu);
    spi::ModelSpec spec;
    spec.path = model;
    spec.imgsz = 960;
    spec.num_classes = 2;
    spec.num_kpts = 0;
    spec.conf_threshold = 0.35f;
    spec.input_nhwc_u8 = false;
    spec.threads = 4;
    AIM_EXPECT_TRUE(be.load(spec));

    // §5.10.1 ①：类别名来自 ONNX metadata.properties['names']
    AIM_EXPECT_EQ(be.class_names().size(), 2u);
    if (be.class_names().size() == 2) {
        AIM_EXPECT_STREQ(be.class_names()[0], "Body");
        AIM_EXPECT_STREQ(be.class_names()[1], "Head");
    }

    std::vector<uint8_t> bgra;
    AIM_EXPECT_TRUE(test::LoadPngAsBgra(std::filesystem::path(image).wstring(), 960, 960, bgra));
    if (bgra.size() != static_cast<size_t>(960) * 960 * 4) return;

    spi::Frame f;
    f.w = 960;
    f.h = 960;
    f.bgra = bgra.data();
    f.valid = true;

    std::vector<Detection> dets;
    AIM_EXPECT_TRUE(be.infer(f, dets));
    Nms(dets, 0.45f); // 与 Pipeline 一致

    AIM_EXPECT_EQ(dets.size(), 2u); // Python ORT: 2
    if (dets.size() == 2) {
        // Body 0.8374 box(450.12,436.88,38.25,91.25)
        AIM_EXPECT_EQ(dets[0].class_id, 0);
        AIM_EXPECT_NEAR(dets[0].score, 0.8374f, 0.05f);
        AIM_EXPECT_NEAR(dets[0].box.x, 450.12f, 3.f);
        AIM_EXPECT_NEAR(dets[0].box.y, 436.88f, 3.f);
        AIM_EXPECT_NEAR(dets[0].box.w, 38.25f, 3.f);

        // Head 0.6406 box(459.25,437.56,11.01,14.38)
        AIM_EXPECT_EQ(dets[1].class_id, 1);
        AIM_EXPECT_NEAR(dets[1].score, 0.6406f, 0.05f);
        AIM_EXPECT_NEAR(dets[1].box.x, 459.25f, 3.f);
        AIM_EXPECT_NEAR(dets[1].box.y, 437.56f, 3.f);
    }

    AIM_EXPECT_TRUE(be.counters().infer_ms >= 0.f);
}

// M9 L2：输出张量用后清零（zero_outputs）不得影响解码结果
AIM_TEST(real_model_zero_outputs_same_result) {
    const std::string model = test::ValorantModelPath();
    if (!test::FileExists(model)) return;
    const std::string image = test::EnvOr(
        "AIM_VALORANT_IMAGE", test::FixturePath("valorant_sample_960.png"));
    if (!test::FileExists(image)) return;

    std::vector<Detection> plain;
    std::vector<Detection> zeroed;
    if (!RunFixture(inference::OrtEp::Cpu, model, image, plain, false)) return;
    if (!RunFixture(inference::OrtEp::Cpu, model, image, zeroed, true)) return;

    AIM_EXPECT_EQ(zeroed.size(), plain.size());
    const std::size_t n = std::min(plain.size(), zeroed.size());
    for (std::size_t i = 0; i < n; ++i) {
        AIM_EXPECT_EQ(zeroed[i].class_id, plain[i].class_id);
        AIM_EXPECT_NEAR(zeroed[i].score, plain[i].score, 1e-4f);
        AIM_EXPECT_NEAR(zeroed[i].box.x, plain[i].box.x, 1e-3f);
    }
}

#ifdef AIM_ENABLE_DML
// DML EP 与 CPU EP 结果一致性：同一 fixture 上类别/置信度/框位置必须对齐。
// 无 DML 设备（CI/无 GPU）或系统 DirectML 过旧时跳过，而非判失败。
AIM_TEST(real_model_dml_matches_cpu) {
    const char* skip = std::getenv("AIM_SKIP_HW_TESTS");
    if (skip && std::string(skip) == "1") return;

    const std::string model = test::ValorantModelPath();
    if (!test::FileExists(model)) return;
    const std::string image = test::EnvOr(
        "AIM_VALORANT_IMAGE", test::FixturePath("valorant_sample_960.png"));
    if (!test::FileExists(image)) return;

    std::vector<Detection> cpu;
    std::vector<Detection> dml;
    if (!RunFixture(inference::OrtEp::Cpu, model, image, cpu)) return;
    if (!RunFixture(inference::OrtEp::Dml, model, image, dml)) return; // 无可用 DML 设备

    AIM_EXPECT_EQ(dml.size(), cpu.size());
    const std::size_t n = std::min(cpu.size(), dml.size());
    for (std::size_t i = 0; i < n; ++i) {
        AIM_EXPECT_EQ(dml[i].class_id, cpu[i].class_id);
        AIM_EXPECT_NEAR(dml[i].score, cpu[i].score, 0.05f);
        AIM_EXPECT_NEAR(dml[i].box.x, cpu[i].box.x, 2.f);
        AIM_EXPECT_NEAR(dml[i].box.y, cpu[i].box.y, 2.f);
        AIM_EXPECT_NEAR(dml[i].box.w, cpu[i].box.w, 2.f);
        AIM_EXPECT_NEAR(dml[i].box.h, cpu[i].box.h, 2.f);
    }
}
#endif // AIM_ENABLE_DML

int main() {
    return aim::test::run_all();
}
