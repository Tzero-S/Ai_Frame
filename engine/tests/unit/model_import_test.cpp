#include "aim_test.h"
#include "fixtures_util.h"
#include "inference/model_registry.h"
#include "inference/model_validator.h"
#include "core/json.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace aim;

AIM_TEST(model_inspect_real_valorant) {
    const std::string path = test::ValorantModelPath();
    if (!test::FileExists(path)) return;

    const auto info = inference::InspectModel(path);
    AIM_EXPECT_TRUE(info.ok);
    if (!info.ok) return;

    AIM_EXPECT_STREQ(info.input.name, "images");
    AIM_EXPECT_EQ(info.input.dtype, 1); // float32
    AIM_EXPECT_EQ(info.input.shape.size(), 4u);
    AIM_EXPECT_EQ(static_cast<int>(info.input.shape[1]), 3);
    AIM_EXPECT_EQ(static_cast<int>(info.input.shape[2]), 960);
    AIM_EXPECT_EQ(static_cast<int>(info.input.shape[3]), 960);
    AIM_EXPECT_FALSE(info.input.dynamic);

    AIM_EXPECT_STREQ(info.output.name, "output0");
    AIM_EXPECT_EQ(info.output.shape.size(), 3u);
    AIM_EXPECT_EQ(static_cast<int>(info.output.shape[1]), 6);
    AIM_EXPECT_EQ(static_cast<int>(info.output.shape[2]), 18900);

    AIM_EXPECT_EQ(info.num_classes, 2);
    AIM_EXPECT_EQ(info.num_kpts, 0);
    AIM_EXPECT_EQ(info.classes.size(), 2u);
    if (info.classes.size() == 2) {
        AIM_EXPECT_STREQ(info.classes[0], "Body");
        AIM_EXPECT_STREQ(info.classes[1], "Head");
    }
    AIM_EXPECT_EQ(info.sha256.size(), 64u);
    AIM_EXPECT_TRUE(!info.created_at.empty());
}

AIM_TEST(model_inspect_test_yolo) {
    const std::string path = test::FixturePath("test_yolo.onnx");
    if (!test::FileExists(path)) return;

    const auto info = inference::InspectModel(path);
    AIM_EXPECT_TRUE(info.ok);
    if (!info.ok) return;

    AIM_EXPECT_STREQ(info.input.name, "input");
    AIM_EXPECT_EQ(static_cast<int>(info.input.shape[2]), 64);
    AIM_EXPECT_STREQ(info.output.name, "output");
    AIM_EXPECT_EQ(static_cast<int>(info.output.shape[1]), 6);
    AIM_EXPECT_EQ(static_cast<int>(info.output.shape[2]), 3);
    AIM_EXPECT_EQ(info.num_classes, 2); // F=6=4+2（无元数据类别）
    AIM_EXPECT_TRUE(info.classes.empty());
}

AIM_TEST(model_inspect_missing) {
    const auto info = inference::InspectModel("no_such_model_xyz.onnx");
    AIM_EXPECT_FALSE(info.ok);
    AIM_EXPECT_TRUE(!info.error.empty());
}

AIM_TEST(sha256_known_vector) {
    const std::string p =
        (std::filesystem::temp_directory_path() / "aim_sha_abc.bin").string();
    {
        std::ofstream f(p, std::ios::binary);
        f << "abc";
    }

    std::string hex;
    std::string err;
    AIM_EXPECT_TRUE(inference::ComputeSha256(p, hex, &err));
    AIM_EXPECT_STREQ(hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::filesystem::remove(p);
}

AIM_TEST(model_info_json_roundtrip) {
    const auto info = inference::InspectModel(test::FixturePath("test_yolo.onnx"));
    if (!info.ok) return;

    const std::string js = inference::ModelInfoToJson(info);
    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(js, v, e));
    AIM_EXPECT_EQ(v["num_classes"].asInt(), 2);
    AIM_EXPECT_STREQ(v["input"]["name"].asString(), "input");
    AIM_EXPECT_EQ(v["input"]["shape"].size(), 4u);
    AIM_EXPECT_EQ(v["input"]["shape"][2].asInt(), 64);
    AIM_EXPECT_STREQ(v["output"]["name"].asString(), "output");
}

AIM_TEST(model_info_to_registry) {
    const auto info = inference::InspectModel(test::FixturePath("test_yolo.onnx"));
    if (!info.ok) return;

    ModelRegistry reg;
    ModelEntry e;
    e.id = info.alias;
    e.alias = info.alias;
    e.file = info.file;
    e.sha256 = info.sha256;
    e.classes = info.classes;
    e.num_kpts = info.num_kpts;
    e.created_at = info.created_at;
    reg.Add(e);

    ModelRegistry reg2;
    AIM_EXPECT_TRUE(reg2.LoadFromJson(reg.ToJson()));
    AIM_EXPECT_TRUE(reg2.Get(info.alias) != nullptr);
}

int main() {
    return aim::test::run_all();
}
