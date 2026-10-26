#include "aim_test.h"
#include "overlay/primitive_batch.h"
#include "overlay/overlay_draw.h"
#include "overlay/overlay_window.h"
#include <algorithm>
#include <cstdlib>
#include <windows.h>
#include <cwctype>
#include <string>
#include <vector>

using namespace aim;
using namespace aim::overlay;

AIM_TEST(primitive_line_geometry) {
    PrimitiveBatch b;
    b.AddLine(0, 0, 10, 0, 0xFF00FF00, 2.f);
    AIM_EXPECT_EQ(b.vertex_count(), 6u);

    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (const auto& v : b.vertices()) {
        minx = std::min(minx, v.x); maxx = std::max(maxx, v.x);
        miny = std::min(miny, v.y); maxy = std::max(maxy, v.y);
    }
    AIM_EXPECT_NEAR(minx, 0.f, 1e-4f);
    AIM_EXPECT_NEAR(maxx, 10.f, 1e-4f);
    AIM_EXPECT_NEAR(miny, -1.f, 1e-4f);
    AIM_EXPECT_NEAR(maxy, 1.f, 1e-4f);
}

AIM_TEST(primitive_counts) {
    PrimitiveBatch b;
    b.AddRect(RectF{0, 0, 10, 10}, 0xFFFFFFFF, 1.f);
    AIM_EXPECT_EQ(b.vertex_count(), 24u);

    b.Clear();
    b.AddCircle(0, 0, 10, 0xFFFFFFFF, 1.f, 8);
    AIM_EXPECT_EQ(b.vertex_count(), 48u);

    b.Clear();
    b.AddCrosshair(0, 0, 5, 0xFFFFFFFF, 1.f);
    AIM_EXPECT_EQ(b.vertex_count(), 12u);
}

AIM_TEST(primitive_clamps) {
    PrimitiveBatch b;
    b.AddLine(0, 0, 10, 0, 0xFFFFFFFF, 0.f); // 厚度钳制到 0.5
    float maxy = -1e9f;
    for (const auto& v : b.vertices()) maxy = std::max(maxy, v.y);
    AIM_EXPECT_NEAR(maxy, 0.25f, 1e-4f);

    b.Clear();
    b.AddCircle(0, 0, 10, 0xFFFFFFFF, 1.f, 2); // 段数钳制到 3
    AIM_EXPECT_EQ(b.vertex_count(), 18u);

    b.Clear();
    b.AddCircle(0, 0, 0, 0xFFFFFFFF, 1.f, 8); // 半径 0 → 无图元
    AIM_EXPECT_EQ(b.vertex_count(), 0u);
}

AIM_TEST(build_overlay_counts) {
    std::vector<Detection> dets(1);
    dets[0].box = RectF{10, 10, 100, 200};

    OverlayStyle st;
    st.circle_segments = 8;
    auto b = BuildOverlay(dets, 1920.f, 1080.f, 100.f, st);
    AIM_EXPECT_EQ(b.vertex_count(), 24u + 12u + 48u); // 框 + 准星 + 圆
}

AIM_TEST(overlay_window_present_hidden) {
    const char* skip = std::getenv("AIM_SKIP_HW_TESTS");
    if (skip && std::string(skip) == "1") return;

    OverlayWindow w;
    std::string err;
    const bool ok = w.init(320, 240, true, &err);
    AIM_EXPECT_TRUE(ok);
    if (!ok) return;

    PrimitiveBatch b;
    b.AddLine(10, 120, 310, 120, 0xFF00FF00, 4.f);
    w.present(b);

    AIM_EXPECT_EQ(w.present_count(), 1);
    AIM_EXPECT_EQ(w.last_vertex_count(), 6);

    const uint32_t* px = w.pixels();
    int nonzero = 0;
    for (int i = 0; i < 320 * 240; ++i) {
        if ((px[i] >> 24) != 0) ++nonzero;
    }
    AIM_EXPECT_TRUE(nonzero > 0);
}

// 去标识：窗口类名与标题都不能带用途字样（EnumWindows/GetClassName 一扫就能读到）
AIM_TEST(overlay_window_is_neutral) {
    const char* skip = std::getenv("AIM_SKIP_HW_TESTS");
    if (skip && std::string(skip) == "1") return;

    OverlayWindow w;
    std::string err;
    const bool ok = w.init(64, 64, true, &err);
    AIM_EXPECT_TRUE(ok);
    if (!ok) return;

    HWND hwnd = static_cast<HWND>(w.handle());
    AIM_EXPECT_TRUE(hwnd != nullptr);
    if (!hwnd) return;

    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, 128);
    wchar_t title[128]{};
    GetWindowTextW(hwnd, title, 128);

    std::wstring c(cls);
    std::wstring t(title);
    for (auto& ch : c) ch = static_cast<wchar_t>(towlower(ch));
    for (auto& ch : t) ch = static_cast<wchar_t>(towlower(ch));

    for (const wchar_t* bad : {L"aim", L"overlay", L"cheat", L"esp"}) {
        AIM_EXPECT_TRUE(c.find(bad) == std::wstring::npos);
        AIM_EXPECT_TRUE(t.find(bad) == std::wstring::npos);
    }
    AIM_EXPECT_EQ(static_cast<int>(t.size()), 0); // 空标题

    // 工具窗口样式：不进任务栏 / Alt-Tab
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    AIM_EXPECT_TRUE((ex & WS_EX_TOOLWINDOW) != 0);
}

int main() {
    return aim::test::run_all();
}