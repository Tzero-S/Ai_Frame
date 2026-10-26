#include "aim_test.h"
#include "core/builtins.h"
#include "core/registry.h"
#include "core/spi.h"
#include "overlay/overlay_spi.h"
#include "overlay/overlay_stub.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace aim;

// SPI 注册名与实现类型的对齐检查（§4.4）：
//   "d3d11" → 真实 OverlayLayer（懒创建窗口）
//   "none"  → NullOverlay（显式关闭）
AIM_TEST(overlay_factory_wiring) {
    RegisterBuiltinExtensions();

    auto real = Registry::create<spi::IOverlay>("d3d11");
    AIM_EXPECT_TRUE(real != nullptr);
    auto* layer = dynamic_cast<overlay::OverlayLayer*>(real.get());
    AIM_EXPECT_TRUE(layer != nullptr);
    // 构造阶段不创建窗口
    AIM_EXPECT_TRUE(!layer->ready());
    AIM_EXPECT_TRUE(std::string(layer->status()) == "uninitialized");

    auto none = Registry::create<spi::IOverlay>("none");
    AIM_EXPECT_TRUE(none != nullptr);
    AIM_EXPECT_TRUE(dynamic_cast<overlay::NullOverlay*>(none.get()) != nullptr);
}

// 边界条件：无效帧不得触发窗口创建，也不得抛异常。
AIM_TEST(overlay_draw_invalid_frame_is_noop) {
    RegisterBuiltinExtensions();

    auto real = Registry::create<spi::IOverlay>("d3d11");
    AIM_EXPECT_TRUE(real != nullptr);

    spi::Frame invalid{}; // valid=false, w/h=0
    real->draw(invalid, {});

    auto* layer = dynamic_cast<overlay::OverlayLayer*>(real.get());
    AIM_EXPECT_TRUE(layer != nullptr);
    AIM_EXPECT_TRUE(!layer->ready());
    AIM_EXPECT_EQ(layer->present_count(), 0);
}

// 尺寸口径：覆盖层按"屏幕尺寸"建窗，而不是按 ROI 帧尺寸（§5.2.2 / §5.11.3）
AIM_TEST(overlay_window_uses_screen_size) {
    const char* skip = std::getenv("AIM_SKIP_HW_TESTS");
    if (skip && std::string(skip) == "1") return;

    RegisterBuiltinExtensions();
    auto real = Registry::create<spi::IOverlay>("d3d11");
    auto* layer = dynamic_cast<overlay::OverlayLayer*>(real.get());
    AIM_EXPECT_TRUE(layer != nullptr);
    if (!layer) return;

    std::vector<uint8_t> px(static_cast<size_t>(640) * 640 * 4, 0);
    spi::Frame f;
    f.valid = true;
    f.w = 640; f.h = 640; f.bgra = px.data();
    f.screen_w = 1920; f.screen_h = 1080;
    f.src_x = 640; f.src_y = 220; f.src_w = 640; f.src_h = 640;

    real->draw(f, {});
    if (!layer->ready()) return; // 无桌面环境时静默跳过（窗口创建失败不算失败）
    AIM_EXPECT_EQ(layer->width(), 1920);
    AIM_EXPECT_EQ(layer->height(), 1080);
    AIM_EXPECT_TRUE(layer->present_count() >= 1);

    // 隐藏/显示不得抛异常
    real->set_visible(false);
    real->set_visible(true);
}

int main() {
    return aim::test::run_all();
}
