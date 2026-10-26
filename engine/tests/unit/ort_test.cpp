#include "aim_test.h"
#include "inference/backend_ort.h"
#include <string>
#include <vector>

using namespace aim;

#ifndef AIM_TEST_FIXTURE_DIR
#  define AIM_TEST_FIXTURE_DIR "tests/fixtures"
#endif

AIM_TEST(ort_backend_name_and_missing_model) {
    inference::OrtBackend cpu(inference::OrtEp::Cpu);
    AIM_EXPECT_STREQ(cpu.backend_name(), "cpu");

    inference::OrtBackend dml(inference::OrtEp::Dml);
    AIM_EXPECT_STREQ(dml.backend_name(), "dml");

    spi::ModelSpec spec;
    spec.path = "no_such_model.onnx";
    AIM_EXPECT_FALSE(cpu.load(spec));
}

AIM_TEST(ort_backend_infer_yolo_model) {
    inference::OrtBackend be(inference::OrtEp::Cpu);
    spi::ModelSpec spec;
    spec.path = std::string(AIM_TEST_FIXTURE_DIR) + "/test_yolo.onnx";
    spec.imgsz = 64;
    spec.num_classes = 2;
    spec.num_kpts = 0;
    spec.conf_threshold = 0.35f;
    spec.input_nhwc_u8 = false; // 测试模型为 float NCHW
    AIM_EXPECT_TRUE(be.load(spec));

    std::vector<uint8_t> bgra(64 * 64 * 4, 128);
    spi::Frame f;
    f.w = 64;
    f.h = 64;
    f.bgra = bgra.data();
    f.valid = true;

    std::vector<Detection> dets;
    AIM_EXPECT_TRUE(be.infer(f, dets));
    AIM_EXPECT_EQ(dets.size(), 2u); // 第三个 anchor conf 0.1 被过滤

    if (dets.size() == 2) {
        AIM_EXPECT_NEAR(dets[0].box.x, 27.f, 1e-3f);
        AIM_EXPECT_NEAR(dets[0].box.y, 22.f, 1e-3f);
        AIM_EXPECT_NEAR(dets[0].box.w, 10.f, 1e-3f);
        AIM_EXPECT_NEAR(dets[0].box.h, 20.f, 1e-3f);
        AIM_EXPECT_NEAR(dets[0].score, 0.9f, 1e-4f);
        AIM_EXPECT_EQ(dets[0].class_id, 1);

        AIM_EXPECT_NEAR(dets[1].box.x, 7.5f, 1e-3f);
        AIM_EXPECT_NEAR(dets[1].box.y, 7.5f, 1e-3f);
        AIM_EXPECT_NEAR(dets[1].score, 0.8f, 1e-4f);
        AIM_EXPECT_EQ(dets[1].class_id, 0);
    }

    AIM_EXPECT_TRUE(be.counters().infer_ms >= 0.f);
}

int main() {
    return aim::test::run_all();
}
