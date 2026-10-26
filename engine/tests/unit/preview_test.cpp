#include "aim_test.h"
#include "fixtures_util.h"
#include "inference/backend_ort.h"
#include "inference/preview.h"
#include "core/base64.h"
#include "core/json.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace aim;

AIM_TEST(base64_known_vectors) {
    const std::vector<uint8_t> a{'a', 'b', 'c'};
    AIM_EXPECT_STREQ(base64::Encode(a), "YWJj");

    const std::vector<uint8_t> b{'a', 'b'};
    AIM_EXPECT_STREQ(base64::Encode(b), "YWI=");

    const std::vector<uint8_t> c{'a'};
    AIM_EXPECT_STREQ(base64::Encode(c), "YQ==");

    std::vector<uint8_t> out;
    std::string err;
    AIM_EXPECT_TRUE(base64::Decode("YWJj", out, &err));
    AIM_EXPECT_EQ(out.size(), 3u);
    AIM_EXPECT_EQ(out[0], 'a');

    AIM_EXPECT_TRUE(base64::Decode("YQ==", out));
    AIM_EXPECT_EQ(out.size(), 1u);

    AIM_EXPECT_FALSE(base64::Decode("!!", out, &err));
}

AIM_TEST(base64_roundtrip) {
    std::vector<uint8_t> data(1000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i * 37 + 11);
    }
    const std::string enc = base64::Encode(data);

    std::vector<uint8_t> dec;
    AIM_EXPECT_TRUE(base64::Decode(enc, dec));
    AIM_EXPECT_EQ(dec.size(), data.size());
    AIM_EXPECT_TRUE(dec == data);
}

namespace {
bool LoadFixture(std::vector<uint8_t>& bgra) {
    const std::string img = test::FixturePath("valorant_sample_960.png");
    if (!test::FileExists(img)) return false;
    return test::LoadPngAsBgra(std::filesystem::path(img).wstring(), 960, 960, bgra);
}
} // namespace

AIM_TEST(preview_real_model) {
    const std::string model = test::ValorantModelPath();
    if (!test::FileExists(model)) return;

    std::vector<uint8_t> bgra;
    if (!LoadFixture(bgra)) return;

    inference::OrtBackend be(inference::OrtEp::Cpu);
    spi::ModelSpec ms;
    ms.path = model;
    ms.imgsz = 960;
    ms.num_classes = 2;
    ms.num_kpts = 0;
    ms.conf_threshold = 0.35f;
    ms.input_nhwc_u8 = false;
    ms.threads = 4;
    AIM_EXPECT_TRUE(be.load(ms));

    spi::Frame f;
    f.w = 960;
    f.h = 960;
    f.bgra = bgra.data();
    f.valid = true;

    inference::PreviewOptions po;
    const auto r = inference::PreviewFrame(be, f, po);
    AIM_EXPECT_TRUE(r.ok);
    AIM_EXPECT_EQ(r.raw_count, 14);
    AIM_EXPECT_EQ(r.detections.size(), 2u);
    AIM_EXPECT_TRUE(r.counters.infer_ms > 0.f);

    // 高置信度阈值 → 0
    po.conf_threshold = 0.9f;
    const auto r2 = inference::PreviewFrame(be, f, po);
    AIM_EXPECT_EQ(r2.detections.size(), 0u);

    // 仅 Head 类别 → 1
    po.conf_threshold = 0.35f;
    po.class_whitelist = {1};
    const auto r3 = inference::PreviewFrame(be, f, po);
    AIM_EXPECT_EQ(r3.detections.size(), 1u);
    if (!r3.detections.empty()) AIM_EXPECT_EQ(r3.detections[0].class_id, 1);

    // 高 IoU 阈值 → 保留更多近重复框
    po.class_whitelist.clear();
    po.iou_threshold = 0.99f;
    const auto r4 = inference::PreviewFrame(be, f, po);
    AIM_EXPECT_TRUE(r4.detections.size() > 2u);

    // 检测 JSON
    const std::string js = inference::DetectionsToJson(r.detections);
    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(js, v, e));
    AIM_EXPECT_EQ(v.size(), 2u);
    AIM_EXPECT_EQ(v[0]["class_id"].asInt(), 0);
    AIM_EXPECT_TRUE(v[0]["score"].asNumber() > 0.5);
}

int main() {
    return aim::test::run_all();
}
