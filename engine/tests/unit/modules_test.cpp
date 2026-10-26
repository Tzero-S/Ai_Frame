#include "aim_test.h"
#include "capture/roi.h"
#include "actuator/dispatcher.h"
#include "inference/model_registry.h"
#include "ipc/handlers.h"
#include "core/clock.h"

using namespace aim;

AIM_TEST(roi_center_square) {
    RoiRect r = ComputeRoi(RoiMode::CenterSquare, 640, 1920, 1080);
    AIM_EXPECT_EQ(r.w, 640);
    AIM_EXPECT_EQ(r.h, 640);
    AIM_EXPECT_EQ(r.x, 640); // (1920-640)/2
    AIM_EXPECT_EQ(r.y, 220); // (1080-640)/2

    RoiRect o = ComputeRoi(RoiMode::CenterSquare, 640, 1920, 1080, 100, 50);
    AIM_EXPECT_EQ(o.x, 740);
    AIM_EXPECT_EQ(o.y, 270);
}

AIM_TEST(roi_full_and_clamp) {
    RoiRect f = ComputeRoi(RoiMode::Full, 640, 1920, 1080);
    AIM_EXPECT_EQ(f.x, 0);
    AIM_EXPECT_EQ(f.y, 0);
    AIM_EXPECT_EQ(f.w, 1920);
    AIM_EXPECT_EQ(f.h, 1080);

    // 边长超过屏幕 → 收缩到短边
    RoiRect c = ComputeRoi(RoiMode::CenterSquare, 2000, 1920, 1080);
    AIM_EXPECT_EQ(c.w, 1080);
    AIM_EXPECT_EQ(c.h, 1080);
    AIM_EXPECT_EQ(c.x, 420); // (1920-1080)/2
    AIM_EXPECT_EQ(c.y, 0);
}

namespace {
struct CountingActuator : spi::IActuator {
    int moves = 0;
    spi::MoveCmd last{};
    void move(const spi::MoveCmd& c) override { last = c; ++moves; }
    void click(const spi::ClickCmd&) override {}
};
} // namespace

AIM_TEST(dispatcher_seq_validation) {
    CountingActuator act;
    Dispatcher d;
    d.SetActuator(&act);

    spi::MoveCmd c1; c1.seq = 1; c1.dx_px = 5.f;
    AIM_EXPECT_TRUE(d.Dispatch(c1));
    AIM_EXPECT_FALSE(d.Dispatch(c1));           // 过期 → 丢弃
    AIM_EXPECT_EQ(d.DroppedCount(), 1u);

    spi::MoveCmd c2; c2.seq = 2;
    AIM_EXPECT_TRUE(d.Dispatch(c2));

    spi::MoveCmd c0; c0.seq = 0;                // 未编号：总是接受
    AIM_EXPECT_TRUE(d.Dispatch(c0));
    AIM_EXPECT_TRUE(d.Dispatch(c0));

    AIM_EXPECT_EQ(act.moves, 4);
    AIM_EXPECT_EQ(d.LastSeq(), 2u);
}

AIM_TEST(dispatcher_without_actuator) {
    Dispatcher d;
    spi::MoveCmd c; c.seq = 1;
    AIM_EXPECT_FALSE(d.Dispatch(c));
}

AIM_TEST(model_registry_load_and_query) {
    const std::string js = R"({
      "models":[
        {"id":"m1","alias":"cs2_yolov8n_640","file":"a.onnx","sha256":"aa",
         "classes":["head","body"],"num_kpts":1,"created_at":"2026-10-25T13:40:00Z"},
        {"id":"m2","alias":"apex_v8s","file":"b.onnx","sha256":"bb",
         "classes":["enemy"],"num_kpts":0,"created_at":"2026-10-25T14:00:00Z"}
      ]
    })";
    ModelRegistry reg;
    std::string err;
    AIM_EXPECT_TRUE(reg.LoadFromJson(js, &err));
    AIM_EXPECT_EQ(reg.Count(), 2u);

    const ModelEntry* m = reg.Get("m1");
    AIM_EXPECT_TRUE(m != nullptr);
    if (m) {
        AIM_EXPECT_STREQ(m->alias, "cs2_yolov8n_640");
        AIM_EXPECT_EQ(m->classes.size(), 2u);
        AIM_EXPECT_EQ(m->num_kpts, 1);
    }
    AIM_EXPECT_TRUE(reg.FindByAlias("apex_v8s") != nullptr);
    AIM_EXPECT_TRUE(reg.FindByAlias("nope") == nullptr);
}

AIM_TEST(model_registry_roundtrip) {
    ModelRegistry a;
    ModelEntry e;
    e.id = "m9";
    e.alias = "test_model";
    e.file = "t.onnx";
    e.sha256 = "cc";
    e.classes = {"head"};
    e.num_kpts = 1;
    e.created_at = "2026-10-25T00:00:00Z";
    a.Add(e);

    ModelRegistry b;
    AIM_EXPECT_TRUE(b.LoadFromJson(a.ToJson()));
    AIM_EXPECT_EQ(b.Count(), 1u);
    const ModelEntry* got = b.Get("m9");
    AIM_EXPECT_TRUE(got != nullptr);
    if (got) {
        AIM_EXPECT_STREQ(got->alias, "test_model");
        AIM_EXPECT_EQ(got->classes.size(), 1u);
        AIM_EXPECT_STREQ(got->classes[0], "head");
    }
}

AIM_TEST(ipc_handler_dispatch) {
    ipc::HandlerRegistry reg;
    reg.Register(ipc::cmd::kEngineHello, [](const ipc::Request&) {
        ipc::Response r;
        r.code = 0;
        r.msg = "ok";
        r.payload["engine_version"] = "1.0.0";
        return r;
    });

    ipc::Request req;
    req.id = "req-7";
    req.cmd = ipc::cmd::kEngineHello;
    ipc::Response resp = reg.Dispatch(req);
    AIM_EXPECT_EQ(resp.code, 0);
    AIM_EXPECT_STREQ(resp.id, "req-7"); // id 回填
    AIM_EXPECT_STREQ(resp.payload["engine_version"].asString(), "1.0.0");

    ipc::Request unknown;
    unknown.id = "req-8";
    unknown.cmd = "no.such.cmd";
    ipc::Response r2 = reg.Dispatch(unknown);
    AIM_EXPECT_EQ(r2.code, 404);
    AIM_EXPECT_STREQ(r2.id, "req-8");
}

AIM_TEST(clock_monotonic) {
    const int64_t a = Clock::NowUs();
    const int64_t b = Clock::NowUs();
    AIM_EXPECT_TRUE(b >= a);
    AIM_EXPECT_TRUE(Clock::NowMs() > 0);
}

int main() {
    return aim::test::run_all();
}
