#include "inference/preprocess.h"

#include <cstddef>

// SSE2 是 x64 的基线指令集，无需额外编译开关。
// AIM_DISABLE_SIMD 仅用于对照基准测试（同一份源码编出标量版本）。
#if !defined(AIM_DISABLE_SIMD) && \
    (defined(_M_X64) || defined(__x86_64__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define AIM_SIMD_SSE2 1
#include <emmintrin.h>
#endif

namespace aim::inference {

namespace {

// 参数守卫：任一非法即返回 false，调用方直接退出。
inline bool Valid(const void* src, const void* dst, int sw, int sh, int dw, int dh) {
    return src && dst && sw > 0 && sh > 0 && dw > 0 && dh > 0;
}

// 源坐标映射（最近邻）。用 int64 中间量避免大尺寸下的乘法溢出。
inline int MapIndex(int dst_i, int dst_n, int src_n) {
    if (dst_n <= 0) return 0;
    const int64_t v = (static_cast<int64_t>(dst_i) * src_n) / dst_n;
    if (v < 0) return 0;
    if (v >= src_n) return src_n - 1;
    return static_cast<int>(v);
}

} // namespace

void FillNchwRgb(const uint8_t* src, int sw, int sh,
                 float* dst, int dw, int dh) {
    if (!Valid(src, dst, sw, sh, dw, dh)) return;

    const size_t plane = static_cast<size_t>(dw) * static_cast<size_t>(dh);
    float* rp = dst;
    float* gp = dst + plane;
    float* bp = dst + 2 * plane;
    constexpr float kInv255 = 1.0f / 255.0f;

    // 无缩放快路径：省掉逐像素映射
    const bool no_scale = (sw == dw && sh == dh);
    for (int y = 0; y < dh; ++y) {
        const int sy = no_scale ? y : MapIndex(y, dh, sh);
        const uint8_t* srow = src + static_cast<size_t>(sy) * static_cast<size_t>(sw) * 4;
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(dw);
        float* rr = rp + row;
        float* gg = gp + row;
        float* bb = bp + row;

        int x = 0;
#ifdef AIM_SIMD_SSE2
        if (no_scale) {
            // 每次处理 4 个像素：解交织出 R/G/B 通道，转 float 后乘 1/255
            const __m128 inv = _mm_set1_ps(kInv255);
            const __m128i mask = _mm_set1_epi32(0xFF);
            for (; x + 4 <= dw; x += 4) {
                const __m128i px =
                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(srow + static_cast<size_t>(x) * 4));
                const __m128i b = _mm_and_si128(px, mask);
                const __m128i g = _mm_and_si128(_mm_srli_epi32(px, 8), mask);
                const __m128i r = _mm_and_si128(_mm_srli_epi32(px, 16), mask);
                _mm_storeu_ps(rr + x, _mm_mul_ps(_mm_cvtepi32_ps(r), inv));
                _mm_storeu_ps(gg + x, _mm_mul_ps(_mm_cvtepi32_ps(g), inv));
                _mm_storeu_ps(bb + x, _mm_mul_ps(_mm_cvtepi32_ps(b), inv));
            }
        }
#endif
        for (; x < dw; ++x) {
            const int sx = no_scale ? x : MapIndex(x, dw, sw);
            const uint8_t* sp = srow + static_cast<size_t>(sx) * 4;
            // BGRA 内存序 → RGB 通道序
            rr[x] = static_cast<float>(sp[2]) * kInv255;
            gg[x] = static_cast<float>(sp[1]) * kInv255;
            bb[x] = static_cast<float>(sp[0]) * kInv255;
        }
    }
}

void FillNchwU8(const uint8_t* src, int sw, int sh,
                uint8_t* dst, int dw, int dh) {
    if (!Valid(src, dst, sw, sh, dw, dh)) return;

    const size_t plane = static_cast<size_t>(dw) * static_cast<size_t>(dh);
    uint8_t* rp = dst;
    uint8_t* gp = dst + plane;
    uint8_t* bp = dst + 2 * plane;

    const bool no_scale = (sw == dw && sh == dh);
    for (int y = 0; y < dh; ++y) {
        const int sy = no_scale ? y : MapIndex(y, dh, sh);
        const uint8_t* srow = src + static_cast<size_t>(sy) * static_cast<size_t>(sw) * 4;
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(dw);
        uint8_t* rr = rp + row;
        uint8_t* gg = gp + row;
        uint8_t* bb = bp + row;
        for (int x = 0; x < dw; ++x) {
            const int sx = no_scale ? x : MapIndex(x, dw, sw);
            const uint8_t* sp = srow + static_cast<size_t>(sx) * 4;
            rr[x] = sp[2];
            gg[x] = sp[1];
            bb[x] = sp[0];
        }
    }
}

} // namespace aim::inference
