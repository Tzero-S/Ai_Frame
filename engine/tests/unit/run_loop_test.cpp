#include "aim_test.h"
#include "core/config.h"
#include "core/config_store.h"
#include "core/json.h"
#include "core/run_loop.h"
#include "core/settings_builder.h"

#include <vector>

using namespace aim;

namespace {

struct FakeCapture : spi::ICapture {
    bool valid = true;
    int w = 640;
    int h = 640;
    bool init(const spi::CaptureSpec&) override { return true; }
    spi::Frame grab(const spi::CaptureSpec&) override {
        spi::Frame f;
        f.valid = valid;
        f.w = w;
        f.h = h;
        f.seq = 1;
        f.ts = 1;
        // 必须提供与 w×h 匹配的真实缓冲：异步采集会把像素 memcpy 走，
        // 谎报尺寸（旧实现只给 4 字节却声明 640×640）会直接越界崩溃。
        const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        if (px_.size() != need) px_.assign(need, 0);
        px_[3] = 255;
        f.bgra = px_.data();
        return f;
    }

private:
    std::vector<uint8_t> px_;
};

struct FakeBackend : spi::IBackend {
    bool infer_ok = true;
    std::vector<Detection> dets;
    bool load(const spi::ModelSpec&) override { return true; }
    bool infer(const spi::Frame&, std::vector<Detection>& out) override {
        out = dets;
        return infer_ok;
    }
    std::string backend_name() const override { return "fake"; }
    spi::PerfCounters counters() const override { return {}; }
};

struct FakeActuator : spi::IActuator {
    int moves = 0;
    int clicks = 0;
    spi::MoveCmd last{};
    void move(const spi::MoveCmd& c) override { last = c; ++moves; }
    void click(const spi::ClickCmd&) override { ++clicks; }
};

Detection MakeDet(float x, float y, float w, float h, float score, int cls = 0) {
    Detection d;
    d.box = RectF{x, y, w, h};
    d.score = score;
    d.class_id = cls;
    return d;
}

// 关闭防检测整形，保证位移可预测
Config StableConfig() {
    Config cfg = ConfigManager::Defaults();
    cfg.anti_detect.level = AntiDetectLevel::Off;
    cfg.inference.target_fps = 240; // 降低测试耗时
    return cfg;
}

} // namespace

// 帧上限：跑满即停，退出码 0
AIM_TEST(run_loop_respects_frame_limit) {
    Config cfg = StableConfig();
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);

    FakeCapture cap;
    FakeBackend be;
    FakeActuator act;

    RunLoop loop;
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 3);
    const int code = loop.Run();

    AIM_EXPECT_EQ(code, 0);
    AIM_EXPECT_EQ(loop.stats().frames, 3);
    AIM_EXPECT_EQ(loop.stats().failures, 0);
}

// 连续失败超限：返回 7（不忙等死循环）
AIM_TEST(run_loop_aborts_on_persistent_failure) {
    Config cfg = StableConfig();
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);

    FakeCapture cap;
    FakeBackend be;
    be.infer_ok = false;
    FakeActuator act;

    RunLoop loop;
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 1); // 上限 = 1*10+100 = 110
    const int code = loop.Run();

    AIM_EXPECT_EQ(code, 7);
    AIM_EXPECT_EQ(loop.stats().frames, 0);
    AIM_EXPECT_TRUE(loop.stats().failures >= 110);
}

// 采集无效帧同样计入失败（不崩溃）
AIM_TEST(run_loop_handles_invalid_frames) {
    Config cfg = StableConfig();
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);

    FakeCapture cap;
    cap.valid = false;
    FakeBackend be;
    FakeActuator act;

    RunLoop loop;
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 1);
    AIM_EXPECT_EQ(loop.Run(), 7);
    AIM_EXPECT_EQ(loop.stats().frames, 0);
}

// 热键激活：按住主键 → 产生位移；松开 → 不动（§5.5.3 Hold 模式）
AIM_TEST(run_loop_hotkey_arms_and_moves) {
    Config cfg = StableConfig();
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);

    FakeCapture cap;
    FakeBackend be;
    FakeActuator act;
    // 模型空间 (300,180,40,80)；FakeCapture 未给 ROI → 按整屏映射：
    //   sx=1920/640=3, sy=1080/640=1.6875 → 屏幕 (900,303.75,120,135)
    //   头部锚点 y = 303.75 + 135*0.12 = 319.95；屏幕中心 540 → dy = -220.05
    be.dets.push_back(MakeDet(300, 180, 40, 80, 0.9f));

    RunLoop loop;
    loop.SetKeyProbe([](int vk) { return vk == 0x02; }); // MOUSE2 持续按住
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 2);
    AIM_EXPECT_EQ(loop.Run(), 0);

    AIM_EXPECT_TRUE(act.moves > 0);
    AIM_EXPECT_NEAR(act.last.dy_px, -220.05f, 0.2f);
}

AIM_TEST(run_loop_without_hotkey_no_move) {
    Config cfg = StableConfig();
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);

    FakeCapture cap;
    FakeBackend be;
    FakeActuator act;
    be.dets.push_back(MakeDet(300, 180, 40, 80, 0.9f));

    RunLoop loop;
    loop.SetKeyProbe([](int) { return false; }); // 全抬起
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 2);
    AIM_EXPECT_EQ(loop.Run(), 0);

    AIM_EXPECT_EQ(act.moves, 0);
}

// 压枪补偿叠加到位移：强度 5 × k 0.2 = 1px 向下（无弹道库时）
AIM_TEST(run_loop_recoil_adds_downward_compensation) {
    Config base = StableConfig();
    const BuiltSettings built = BuildSettings(base, 1920, 1080);

    // 关闭压枪
    Config off = base;
    off.weapon.recoil.enabled = false;
    FakeCapture c1; FakeBackend b1; FakeActuator a1;
    b1.dets.push_back(MakeDet(300, 180, 40, 80, 0.9f));
    RunLoop l1;
    l1.SetKeyProbe([](int vk) { return vk == 0x02; });
    l1.InitWithDeps(off, built, &c1, &b1, &a1, nullptr, 2);
    AIM_EXPECT_EQ(l1.Run(), 0);
    AIM_EXPECT_TRUE(a1.moves > 0);

    // 开启压枪 + 按住左键（默认开火键）
    FakeCapture c2; FakeBackend b2; FakeActuator a2;
    b2.dets.push_back(MakeDet(300, 180, 40, 80, 0.9f));
    RunLoop l2;
    l2.SetKeyProbe([](int vk) { return vk == 0x02 || vk == 0x01; });
    l2.InitWithDeps(base, built, &c2, &b2, &a2, nullptr, 2);
    AIM_EXPECT_EQ(l2.Run(), 0);
    AIM_EXPECT_TRUE(a2.moves > 0);

    // 补偿量现在按"开火时长"累计（量纲修复后），2 帧内只有几毫秒，
    // 绝对值极小且依赖墙钟，因此这里只断言方向：开压枪后下压量必须更大。
    // 具体幅度由 recoil_pattern_test 的增量累加用例保证。
    AIM_EXPECT_TRUE(a2.last.dy_px > a1.last.dy_px);
}

// 扳机：准星命中 + 按键按下 → 下发点击（步枪延时 0）
AIM_TEST(run_loop_trigger_clicks_on_hit) {
    Config cfg = StableConfig();
    cfg.weapon.trigger.enabled = true;
    cfg.weapon.trigger.type = TriggerType::Rifle;
    cfg.weapon.trigger.key = "MOUSE5";
    cfg.weapon.trigger.rifle_delay_ms = 0;
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);

    FakeCapture cap;
    FakeBackend be;
    FakeActuator act;
    // 模型 (300,280,40,80) → 屏幕 (900,472.5,120,135)：x 覆盖 960，
    // 纵向中心 540 == 准星 y（±10% 容差 13.5px 内）
    be.dets.push_back(MakeDet(300, 280, 40, 80, 0.9f));

    RunLoop loop;
    loop.SetKeyProbe([](int vk) { return vk == 0x06; }); // MOUSE5 按住
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 2);
    AIM_EXPECT_EQ(loop.Run(), 0);

    AIM_EXPECT_TRUE(act.clicks > 0);
}

// 扳机未绑定键 → 不生效（§5.7.2 前置校验）
AIM_TEST(run_loop_trigger_requires_key) {
    Config cfg = StableConfig();
    cfg.weapon.trigger.enabled = true;
    cfg.weapon.trigger.key = ""; // 未绑定
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);

    FakeCapture cap;
    FakeBackend be;
    FakeActuator act;
    be.dets.push_back(MakeDet(300, 140, 40, 80, 0.9f));

    RunLoop loop;
    loop.SetKeyProbe([](int) { return true; });
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 2);
    AIM_EXPECT_EQ(loop.Run(), 0);

    AIM_EXPECT_EQ(act.clicks, 0);
}

// 热重载：配置变更在运行中生效（非设备字段）
AIM_TEST(run_loop_hot_reload_applies_settings) {
    Config cfg = StableConfig();
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);
    ConfigStore store(cfg);

    FakeCapture cap;
    FakeBackend be;
    FakeActuator act;

    RunLoop loop;
    loop.AttachConfigStore(&store);
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 2);

    json::Value patch;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(R"({"aim":{"scope":{"half_w":500}}})", patch, e));
    AIM_EXPECT_TRUE(store.ApplyMerge(patch).ok());

    AIM_EXPECT_EQ(loop.Run(), 0);
    AIM_EXPECT_NEAR(loop.settings().pipeline.scope.half_w, 500.f, 1e-3f);
    AIM_EXPECT_FALSE(loop.restart_required());
}

// 设备相关字段变更：标记需重启，运行参数保持不变
AIM_TEST(run_loop_marks_restart_on_device_change) {
    Config cfg = StableConfig();
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);
    ConfigStore store(cfg);

    FakeCapture cap;
    FakeBackend be;
    FakeActuator act;

    RunLoop loop;
    loop.AttachConfigStore(&store);
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 2);

    json::Value patch;
    std::string e;
    AIM_EXPECT_TRUE(json::parse(R"({"capture":{"roi":{"edge":320}}})", patch, e));
    AIM_EXPECT_TRUE(store.ApplyMerge(patch).ok());

    AIM_EXPECT_EQ(loop.Run(), 0);
    AIM_EXPECT_TRUE(loop.restart_required());
    // 未热切：ROI 仍是启动时的 640
    AIM_EXPECT_EQ(loop.settings().capture_spec.roi_w, 640);
}

int main() {
    return aim::test::run_all();
}

// 异步采集集成：与生产一致走「采集泵 + 双缓冲」，帧数与统计应正常
AIM_TEST(run_loop_async_capture_path) {
    Config cfg = StableConfig();
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);

    FakeCapture cap;
    FakeBackend be;
    FakeActuator act;

    RunLoop loop;
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 5, /*async=*/true);
    AIM_EXPECT_EQ(loop.Run(), 0);
    AIM_EXPECT_EQ(loop.stats().frames, 5);
    AIM_EXPECT_EQ(loop.stats().failures, 0);
}

// 异步 + 采集源彻底死掉：看门狗必须中止循环，不能无限空转
// （这是并行化引入的新失效模式：无帧不再等价于采集失败）
AIM_TEST(run_loop_async_dead_capture_aborts) {
    Config cfg = StableConfig();
    const BuiltSettings built = BuildSettings(cfg, 1920, 1080);

    FakeCapture cap;
    cap.valid = false; // 采集永远返回无效帧
    FakeBackend be;
    FakeActuator act;

    RunLoop loop;
    loop.InitWithDeps(cfg, built, &cap, &be, &act, nullptr, 1, /*async=*/true);
    AIM_EXPECT_EQ(loop.Run(), 7); // 看门狗触发 abort
    AIM_EXPECT_EQ(loop.stats().frames, 0);
}
