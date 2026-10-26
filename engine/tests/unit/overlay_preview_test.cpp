#include "aim_test.h"
#include "core/json.h"
#include "core/runtime.h"
#include "overlay/overlay_preview.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace aim;

AIM_TEST(overlay_preview_render_hidden) {
    const char* skip = std::getenv("AIM_SKIP_HW_TESTS");
    if (skip && std::string(skip) == "1") return;

    overlay::OverlayPreview ov;
    std::string err;
    AIM_EXPECT_TRUE(ov.init(320, 240, true, &err));

    std::vector<Detection> dets(2);
    dets[0].box = RectF{10, 10, 100, 200};
    dets[0].class_id = 0;
    dets[0].score = 0.9f;
    dets[1].box = RectF{150, 20, 40, 50};
    dets[1].class_id = 1;
    dets[1].score = 0.8f;

    ov.render(dets, 320.f, 240.f, 80.f);
    AIM_EXPECT_EQ(ov.present_count(), 1);
    AIM_EXPECT_TRUE(ov.last_vertex_count() > 0);

    const uint32_t* px = ov.pixels();
    int nonzero = 0;
    for (int i = 0; i < 320 * 240; ++i) {
        if ((px[i] >> 24) != 0) ++nonzero;
    }
    AIM_EXPECT_TRUE(nonzero > 0);
}

AIM_TEST(runtime_snapshot_detections) {
    EngineRuntime rt;
    rt.Start(0);

    Detection d;
    d.box = RectF{1, 2, 3, 4};
    d.score = 0.5f;
    d.class_id = 1;
    rt.SetDetections({d});

    const std::string js = rt.SnapshotJson(1000);
    json::Value v;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(js, v, e));
    AIM_EXPECT_EQ(v["detections"].size(), 1u);
    AIM_EXPECT_EQ(v["detections"][0]["class_id"].asInt(), 1);
    AIM_EXPECT_NEAR(v["detections"][0]["score"].asNumber(), 0.5, 1e-6);
}

int main() {
    return aim::test::run_all();
}
