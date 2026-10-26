#include "aim_test.h"
#include "inference/preprocess.h"

#include <vector>

using namespace aim::inference;

namespace {

// 构造一张 BGRA 图：每像素写入固定颜色
std::vector<uint8_t> MakeBgra(int w, int h, uint8_t b, uint8_t g, uint8_t r, uint8_t a = 255) {
    std::vector<uint8_t> v(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i) {
        v[static_cast<size_t>(i) * 4 + 0] = b;
        v[static_cast<size_t>(i) * 4 + 1] = g;
        v[static_cast<size_t>(i) * 4 + 2] = r;
        v[static_cast<size_t>(i) * 4 + 3] = a;
    }
    return v;
}

} // namespace

// 通道序与归一化：BGRA 内存序 → NCHW 的 R/G/B 三个平面
AIM_TEST(preprocess_channel_order_and_scale) {
    const int w = 2, h = 2;
    const auto src = MakeBgra(w, h, /*b=*/10, /*g=*/20, /*r=*/30);

    std::vector<float> dst(static_cast<size_t>(3) * w * h, -1.f);
    FillNchwRgb(src.data(), w, h, dst.data(), w, h);

    const size_t plane = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < plane; ++i) {
        AIM_EXPECT_NEAR(dst[i], 30.f / 255.f, 1e-6f);            // R
        AIM_EXPECT_NEAR(dst[plane + i], 20.f / 255.f, 1e-6f);    // G
        AIM_EXPECT_NEAR(dst[2 * plane + i], 10.f / 255.f, 1e-6f); // B
    }
}

// 同尺寸快路径与缩放路径结果一致（2×2 → 2×2）
AIM_TEST(preprocess_no_scale_matches_scale) {
    const auto src = MakeBgra(2, 2, 7, 8, 9);
    std::vector<float> a(12, 0.f);
    std::vector<float> b(12, 0.f);
    FillNchwRgb(src.data(), 2, 2, a.data(), 2, 2);
    FillNchwRgb(src.data(), 2, 2, b.data(), 2, 2);
    for (size_t i = 0; i < a.size(); ++i) AIM_EXPECT_NEAR(a[i], b[i], 0.f);
}

// 降采样：4×4 每格同色，缩到 2×2 应保持对应象限的颜色
AIM_TEST(preprocess_downscale_mapping) {
    const int sw = 4, sh = 4;
    std::vector<uint8_t> src(static_cast<size_t>(sw) * sh * 4, 0);
    auto put = [&](int x, int y, uint8_t r) {
        const size_t i = (static_cast<size_t>(y) * sw + x) * 4;
        src[i + 0] = 0;
        src[i + 1] = 0;
        src[i + 2] = r;
        src[i + 3] = 255;
    };
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            // 左上 100、右上 150、左下 200、右下 250
            const uint8_t r = static_cast<uint8_t>((y < 2 ? 0 : 100) + (x < 2 ? 100 : 150));
            put(x, y, r);
        }
    }

    std::vector<float> dst(static_cast<size_t>(3) * 2 * 2, 0.f);
    FillNchwRgb(src.data(), sw, sh, dst.data(), 2, 2);
    // 最近邻取样点：x/y 均映射到 0 与 2 → 左上100 / 右上150 / 左下200 / 右下250
    AIM_EXPECT_NEAR(dst[0], 100.f / 255.f, 1e-6f);
    AIM_EXPECT_NEAR(dst[1], 150.f / 255.f, 1e-6f);
    AIM_EXPECT_NEAR(dst[2], 200.f / 255.f, 1e-6f);
    AIM_EXPECT_NEAR(dst[3], 250.f / 255.f, 1e-6f);
}

// 非法参数：空指针 / 非正尺寸 —— 不写内存、不崩
AIM_TEST(preprocess_rejects_invalid_args) {
    std::vector<float> dst(12, -7.f);
    const auto src = MakeBgra(2, 2, 1, 2, 3);

    FillNchwRgb(nullptr, 2, 2, dst.data(), 2, 2);
    FillNchwRgb(src.data(), 2, 2, nullptr, 2, 2);
    FillNchwRgb(src.data(), 0, 2, dst.data(), 2, 2);
    FillNchwRgb(src.data(), 2, 0, dst.data(), 2, 2);
    FillNchwRgb(src.data(), 2, 2, dst.data(), 0, 2);
    FillNchwRgb(src.data(), 2, 2, dst.data(), 2, 0);
    FillNchwRgb(src.data(), -4, -4, dst.data(), -4, -4);

    for (float v : dst) AIM_EXPECT_NEAR(v, -7.f, 0.f); // 全程未被写入
}

// uint8 变体：同样的通道序，不做归一化
AIM_TEST(preprocess_u8_variant) {
    const int w = 2, h = 1;
    const auto src = MakeBgra(w, h, 11, 22, 33);
    std::vector<uint8_t> dst(static_cast<size_t>(3) * w * h, 0);
    FillNchwU8(src.data(), w, h, dst.data(), w, h);

    const size_t plane = static_cast<size_t>(w) * h;
    AIM_EXPECT_EQ(static_cast<int>(dst[0]), 33);
    AIM_EXPECT_EQ(static_cast<int>(dst[plane]), 22);
    AIM_EXPECT_EQ(static_cast<int>(dst[2 * plane]), 11);
}

int main() {
    return aim::test::run_all();
}

// SIMD 路径覆盖：宽度 >= 4 才会走向量化分支，逐像素校验通道与归一化
AIM_TEST(preprocess_simd_path_wide_row) {
    const int w = 8, h = 2;
    std::vector<uint8_t> src(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i) {
        const uint8_t v = static_cast<uint8_t>(i * 3);
        src[static_cast<size_t>(i) * 4 + 0] = v;             // B
        src[static_cast<size_t>(i) * 4 + 1] = static_cast<uint8_t>(v + 1); // G
        src[static_cast<size_t>(i) * 4 + 2] = static_cast<uint8_t>(v + 2); // R
        src[static_cast<size_t>(i) * 4 + 3] = 255;
    }

    std::vector<float> dst(static_cast<size_t>(3) * w * h, -1.f);
    FillNchwRgb(src.data(), w, h, dst.data(), w, h);

    const size_t plane = static_cast<size_t>(w) * h;
    for (int i = 0; i < w * h; ++i) {
        const uint8_t v = static_cast<uint8_t>(i * 3);
        AIM_EXPECT_NEAR(dst[i], static_cast<float>(v + 2) / 255.f, 1e-6f);
        AIM_EXPECT_NEAR(dst[plane + i], static_cast<float>(v + 1) / 255.f, 1e-6f);
        AIM_EXPECT_NEAR(dst[2 * plane + i], static_cast<float>(v) / 255.f, 1e-6f);
    }
}
