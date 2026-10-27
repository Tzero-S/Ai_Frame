#include "aim_test.h"
#include "postprocess/yolo_decoder.h"
#include "postprocess/nms.h"
#include "postprocess/class_filter.h"
#include <vector>
#include <cmath>

using namespace aim;

AIM_TEST(yolo_decode_feat_first_no_obj) {
    // [1,6,3]，nc=2，无 obj（F=4+2=6）
    std::vector<float> data = {
        320, 100, 0.5f,      // cx
        320, 100, 0.5f,      // cy
        100, 50,  0.2f,      // w
        200, 50,  0.4f,      // h
        0.2f, 0.8f, 0.1f,    // cls0
        0.9f, 0.1f, 0.1f     // cls1
    };
    TensorView tv{data.data(), {1, 6, 3}, 3};
    YoloOutputSpec spec; spec.num_classes = 2; spec.num_kpts = 0;
    spec.imgsz = 640; spec.conf_threshold = 0.35f;

    auto dets = DecodeYolo(tv, spec);
    AIM_EXPECT_EQ(dets.size(), 2u); // 第三个 anchor conf 0.1 被过滤
    AIM_EXPECT_NEAR(dets[0].box.x, 270.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].box.y, 220.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].box.w, 100.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].box.h, 200.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].score, 0.9f, 1e-5f);
    AIM_EXPECT_EQ(dets[0].class_id, 1);
    AIM_EXPECT_NEAR(dets[1].box.x, 75.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[1].score, 0.8f, 1e-5f);
    AIM_EXPECT_EQ(dets[1].class_id, 0);
}

AIM_TEST(yolo_decode_anchor_first_layout) {
    // [1,3,6]（anchor 在前），逻辑值与上一用例相同
    std::vector<float> data = {
        320, 320, 100, 200, 0.2f, 0.9f,   // anchor0
        100, 100, 50,  50,  0.8f, 0.1f,   // anchor1
        0.5f, 0.5f, 0.2f, 0.4f, 0.1f, 0.1f // anchor2
    };
    TensorView tv{data.data(), {1, 3, 6}, 3};
    YoloOutputSpec spec; spec.num_classes = 2; spec.num_kpts = 0;
    spec.imgsz = 640; spec.conf_threshold = 0.35f;

    auto dets = DecodeYolo(tv, spec);
    AIM_EXPECT_EQ(dets.size(), 2u);
    AIM_EXPECT_NEAR(dets[0].box.x, 270.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].box.y, 220.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].score, 0.9f, 1e-5f);
    AIM_EXPECT_EQ(dets[0].class_id, 1);
}

AIM_TEST(yolo_decode_with_obj) {
    // [1,7,2]，nc=2，含 obj（F=5+2=7）
    std::vector<float> data = {
        320, 100,   // cx
        320, 100,   // cy
        100, 50,    // w
        200, 50,    // h
        0.9f, 0.2f, // obj
        0.1f, 0.9f, // cls0
        0.8f, 0.1f  // cls1
    };
    TensorView tv{data.data(), {1, 7, 2}, 3};
    YoloOutputSpec spec; spec.num_classes = 2; spec.num_kpts = 0;
    spec.imgsz = 640; spec.conf_threshold = 0.35f;

    auto dets = DecodeYolo(tv, spec);
    AIM_EXPECT_EQ(dets.size(), 1u); // 第二个 obj*cls=0.18 被过滤
    AIM_EXPECT_NEAR(dets[0].score, 0.9f * 0.8f, 1e-5f);
    AIM_EXPECT_EQ(dets[0].class_id, 1);
    AIM_EXPECT_NEAR(dets[0].box.x, 270.f, 1e-3f);
}

AIM_TEST(yolo_decode_special9_layout) {
    // feats==9 && nkpt==1 → [cx,cy,w,h,obj,kx,ks,ky,?]
    std::vector<float> data = {320, 320, 100, 200, 0.8f, 320, 0.9f, 100, 0.0f};
    TensorView tv{data.data(), {1, 9, 1}, 3};
    YoloOutputSpec spec; spec.num_classes = 1; spec.num_kpts = 1;
    spec.imgsz = 640; spec.conf_threshold = 0.35f;

    auto dets = DecodeYolo(tv, spec);
    AIM_EXPECT_EQ(dets.size(), 1u);
    AIM_EXPECT_NEAR(dets[0].score, 0.8f, 1e-5f);
    AIM_EXPECT_EQ(dets[0].class_id, 0);
    AIM_EXPECT_EQ(dets[0].kpts.size(), 1u);
    AIM_EXPECT_NEAR(dets[0].kpts[0].x, 320.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].kpts[0].y, 100.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].kpts[0].score, 0.9f, 1e-5f);
    AIM_EXPECT_NEAR(dets[0].box.x, 270.f, 1e-3f);
}

AIM_TEST(scale_to_screen) {
    std::vector<Detection> dets(1);
    dets[0].box = RectF{0, 0, 640, 640};
    dets[0].kpts.push_back(Keypoint{320, 320, 0.9f});
    ScaleToScreen(dets, 640, 640, 1920, 1080);
    AIM_EXPECT_NEAR(dets[0].box.w, 1920.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].box.h, 1080.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].kpts[0].x, 960.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].kpts[0].y, 540.f, 1e-3f);
}

AIM_TEST(scale_roi_to_screen_offset_and_scale) {
    // 采集 ROI：屏幕 2560×1600 中心 960×960 裁剪，原点 (800,320)；模型输入 480×480
    std::vector<Detection> dets(1);
    dets[0].box = RectF{0, 0, 480, 480};
    dets[0].kpts.push_back(Keypoint{240, 240, 0.9f});
    ScaleRoiToScreen(dets, 480, 480, 800.f, 320.f, 960.f, 960.f);
    // 缩放 = 960/480 = 2；整框正好覆盖 ROI
    AIM_EXPECT_NEAR(dets[0].box.x, 800.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].box.y, 320.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].box.w, 960.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].box.h, 960.f, 1e-3f);
    AIM_EXPECT_NEAR(dets[0].kpts[0].x, 1280.f, 1e-3f); // ROI 中心 == 屏幕中心
    AIM_EXPECT_NEAR(dets[0].kpts[0].y, 800.f, 1e-3f);
}

AIM_TEST(scale_roi_to_screen_guards) {
    std::vector<Detection> dets(1);
    dets[0].box = RectF{10, 20, 30, 40};
    // 非法入参：不得改动、不得崩溃
    ScaleRoiToScreen(dets, 0.f, 640.f, 0.f, 0.f, 640.f, 640.f);
    ScaleRoiToScreen(dets, 640.f, 640.f, 0.f, 0.f, 0.f, 640.f);
    AIM_EXPECT_NEAR(dets[0].box.x, 10.f, 1e-6f);
    AIM_EXPECT_NEAR(dets[0].box.y, 20.f, 1e-6f);
}

AIM_TEST(iou_and_nms) {
    AIM_EXPECT_NEAR(Iou(RectF{0, 0, 100, 100}, RectF{10, 0, 100, 100}), 9000.f / 11000.f, 1e-5f);
    // min/max 归一化：x1>x2 的框应与正常框等价
    AIM_EXPECT_NEAR(Iou(RectF{100, 0, -100, 100}, RectF{0, 0, 100, 100}), 1.f, 1e-5f);

    std::vector<Detection> dets(3);
    dets[0].box = RectF{0, 0, 100, 100};   dets[0].score = 0.9f;
    dets[1].box = RectF{10, 0, 100, 100};  dets[1].score = 0.8f;
    dets[2].box = RectF{500, 500, 50, 50}; dets[2].score = 0.5f;

    Nms(dets, 0.45f);
    AIM_EXPECT_EQ(dets.size(), 2u);
    AIM_EXPECT_NEAR(dets[0].score, 0.9f, 1e-5f);
    AIM_EXPECT_NEAR(dets[1].score, 0.5f, 1e-5f);

    // 阈值高于 IoU(0.818) 时不应抑制
    std::vector<Detection> dets2(2);
    dets2[0].box = RectF{0, 0, 100, 100};  dets2[0].score = 0.9f;
    dets2[1].box = RectF{10, 0, 100, 100}; dets2[1].score = 0.8f;
    Nms(dets2, 0.9f);
    AIM_EXPECT_EQ(dets2.size(), 2u);
}

AIM_TEST(class_filter_whitelist) {
    std::vector<Detection> dets(3);
    dets[0].class_id = 0;
    dets[1].class_id = 1;
    dets[2].class_id = 2;

    FilterClasses(dets, {1});
    AIM_EXPECT_EQ(dets.size(), 1u);
    AIM_EXPECT_EQ(dets[0].class_id, 1);

    std::vector<Detection> all(2);
    all[0].class_id = 0; all[1].class_id = 2;
    FilterClasses(all, {}); // 空白名单 → 不过滤
    AIM_EXPECT_EQ(all.size(), 2u);
}

// P0-2 回归：类别数大于实际特征数时必须返回空结果 + 报错，而不是越界读。
// 场景来源：模型输出特征维为动态、metadata 声明了 80 类，运行期实际只输出 6 特征。
AIM_TEST(yolo_decode_rejects_feat_shortfall) {
    // 6 个特征（cx,cy,w,h,cls0,cls1）却声明 80 类
    std::vector<float> data(6 * 4, 0.0f);
    for (int a = 0; a < 4; ++a) {
        data[0 * 4 + a] = 100.f; // cx
        data[1 * 4 + a] = 100.f; // cy
        data[2 * 4 + a] = 20.f;  // w
        data[3 * 4 + a] = 40.f;  // h
        data[4 * 4 + a] = 0.9f;  // cls0
        data[5 * 4 + a] = 0.1f;  // cls1
    }
    TensorView tv;
    tv.data = data.data();
    tv.ndim = 3;
    tv.dims[0] = 1;
    tv.dims[1] = 6;  // feats
    tv.dims[2] = 4;  // anchors

    YoloOutputSpec spec;
    spec.num_classes = 80; // 与 feats 不符
    spec.num_kpts = 0;
    spec.imgsz = 640;
    spec.conf_threshold = 0.3f;

    std::string err;
    const auto dets = DecodeYolo(tv, spec, &err);
    AIM_EXPECT_TRUE(dets.empty());
    AIM_EXPECT_TRUE(!err.empty());
    AIM_EXPECT_TRUE(err.find("特征维不足") != std::string::npos);

    // 同样的张量、类别数改成 2 → 正常解出
    YoloOutputSpec ok = spec;
    ok.num_classes = 2;
    std::string err2;
    const auto dets2 = DecodeYolo(tv, ok, &err2);
    AIM_EXPECT_EQ(dets2.size(), 4u);
    AIM_EXPECT_TRUE(err2.empty());
}

// special9 布局（1 关键点）走单独判据：feat_count 必须 >= 8（cx,cy,w,h,obj,kx,ks,ky）
AIM_TEST(yolo_decode_special9_guard) {
    // 缓冲与 dims 严格一致（避免测试自己越界）：7 特征 × 3 锚点
    std::vector<float> short_data(7 * 3, 0.5f);
    TensorView tv;
    tv.data = short_data.data();
    tv.ndim = 3;
    tv.dims[0] = 1;
    tv.dims[1] = 7; // 7 < 8 → 必须被拒
    tv.dims[2] = 3;

    YoloOutputSpec spec;
    spec.num_classes = 1;
    spec.num_kpts = 1; // 触发 special9 判定分支
    spec.imgsz = 640;
    spec.conf_threshold = 0.3f;

    std::string err;
    const auto dets = DecodeYolo(tv, spec, &err);
    AIM_EXPECT_TRUE(dets.empty());
    AIM_EXPECT_TRUE(err.find("特征维不足") != std::string::npos);

    // 补齐到 8 特征 → 正常解出
    std::vector<float> ok_data(8 * 3, 0.5f);
    for (int a = 0; a < 3; ++a) {
        ok_data[0 * 3 + a] = 300.f; // cx
        ok_data[1 * 3 + a] = 300.f; // cy
        ok_data[2 * 3 + a] = 20.f;  // w
        ok_data[3 * 3 + a] = 40.f;  // h
        ok_data[4 * 3 + a] = 0.9f;  // obj
        ok_data[5 * 3 + a] = 300.f; // kx
        ok_data[6 * 3 + a] = 0.9f;  // ks
        ok_data[7 * 3 + a] = 300.f; // ky
    }
    TensorView tv2;
    tv2.data = ok_data.data();
    tv2.ndim = 3;
    tv2.dims[0] = 1;
    tv2.dims[1] = 8;
    tv2.dims[2] = 3;
    std::string err2;
    const auto dets2 = DecodeYolo(tv2, spec, &err2);
    AIM_EXPECT_EQ(dets2.size(), 3u);
    AIM_EXPECT_TRUE(dets2[0].kpts.size() == 1u);
}

int main() {
    return aim::test::run_all();
}

// 按类别 NMS：同类重叠抑制、异类重叠共存（body/head 场景）
AIM_TEST(nms_per_class_keeps_overlapping_classes) {
    // 两个几乎重合的框，分属不同类别
    std::vector<Detection> dets(2);
    dets[0].box = RectF{100, 100, 200, 200};
    dets[0].score = 0.90f;
    dets[0].class_id = 0; // body
    dets[1].box = RectF{110, 110, 190, 190};
    dets[1].score = 0.85f;
    dets[1].class_id = 1; // head

    auto per_class = dets;
    Nms(per_class, 0.45f); // 默认 PerClass
    AIM_EXPECT_EQ(per_class.size(), 2u); // 异类不互相压制

    auto agnostic = dets;
    Nms(agnostic, 0.45f, NmsMode::Agnostic);
    AIM_EXPECT_EQ(agnostic.size(), 1u); // 旧行为：跨类压制
    if (!agnostic.empty()) AIM_EXPECT_NEAR(agnostic[0].score, 0.90f, 1e-5f);
}

// 按类别 NMS：同类仍然抑制，且多类别可各自保留
AIM_TEST(nms_per_class_still_suppresses_same_class) {
    std::vector<Detection> dets(3);
    dets[0].box = RectF{0, 0, 100, 100};     dets[0].score = 0.9f; dets[0].class_id = 0;
    dets[1].box = RectF{10, 0, 100, 100};    dets[1].score = 0.8f; dets[1].class_id = 0; // 同类重叠
    dets[2].box = RectF{12, 0, 100, 100};    dets[2].score = 0.7f; dets[2].class_id = 2; // 异类

    Nms(dets, 0.45f);
    AIM_EXPECT_EQ(dets.size(), 2u);
    if (dets.size() == 2) {
        AIM_EXPECT_NEAR(dets[0].score, 0.9f, 1e-5f);
        AIM_EXPECT_EQ(dets[1].class_id, 2);
    }
}
