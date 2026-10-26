#include "postprocess/yolo_decoder.h"
#include <cmath>
#include <algorithm>

namespace aim {

namespace {
inline float Sigmoid(float x) { return 1.f / (1.f + std::exp(-x)); }
// §7.3：obj/cls/ks 若不在 [0,1] 则过 sigmoid
inline float MaybeSigmoid(float x) { return (x >= 0.f && x <= 1.f) ? x : Sigmoid(x); }

// §7.3 "特征维特征值集合"
bool IsFeatLike(int n) {
    static const int kFeatLike[] = {6, 7, 8, 9, 17, 80, 81, 84, 85, 116};
    for (int v : kFeatLike) if (v == n) return true;
    return false;
}
} // namespace

std::vector<Detection> DecodeYolo(const TensorView& out, const YoloOutputSpec& spec,
                                  std::string* err) {
    std::vector<Detection> dets;
    if (!out.data) { if (err) *err = "输出张量为空"; return dets; }
    if (out.ndim != 3) { if (err) *err = "期望 3 维输出 [1,F,A] 或 [1,A,F]"; return dets; }

    const int d1 = out.dims[1];
    const int d2 = out.dims[2];
    int feat_axis, anchor_axis;
    if (IsFeatLike(d2))      { feat_axis = 2; anchor_axis = 1; }
    else if (IsFeatLike(d1)) { feat_axis = 1; anchor_axis = 2; }
    else {
        feat_axis = (d1 <= d2) ? 1 : 2;
        anchor_axis = 3 - feat_axis;
    }

    const int feat_count = out.dims[feat_axis];
    const int anchor_count = out.dims[anchor_axis];
    if (feat_count < 5 || anchor_count <= 0) {
        if (err) *err = "特征维/锚点维非法";
        return dets;
    }

    const int nc = spec.num_classes;
    const int nk = spec.num_kpts;
    // §7.3 特殊布局：feats==9 && num_kpts==1 → [cx,cy,w,h,obj,kx,ks,ky,?]
    const bool special9 = (feat_count == 9 && nk == 1);

    bool has_obj = spec.has_obj;
    if (spec.auto_detect_obj && !special9) {
        const int no_obj = 4 + nc + 3 * nk;
        const int with_obj = 5 + nc + 3 * nk;
        if (feat_count == with_obj)      has_obj = true;
        else if (feat_count == no_obj)   has_obj = false;
        else                             has_obj = (feat_count >= with_obj);
    }

    auto at = [&](int f, int a) -> float {
        return (feat_axis == 1) ? out.data[f * anchor_count + a]
                                : out.data[a * feat_count + f];
    };
    // 归一化坐标（≤1.5）乘 imgsz 折回像素
    auto to_px = [&](float v) -> float {
        return (std::fabs(v) <= 1.5f) ? v * static_cast<float>(spec.imgsz) : v;
    };

    dets.reserve(static_cast<size_t>(anchor_count) / 4 + 1);
    for (int a = 0; a < anchor_count; ++a) {
        const float cx = at(0, a), cy = at(1, a), w = at(2, a), h = at(3, a);
        Detection d;

        if (special9) {
            const float obj = MaybeSigmoid(at(4, a));
            const float kx = at(5, a);
            const float ks = MaybeSigmoid(at(6, a));
            const float ky = at(7, a);
            d.score = obj;
            d.class_id = 0;
            d.kpts.push_back(Keypoint{ to_px(kx), to_px(ky), ks });
        } else {
            int idx = 4;
            float obj = 1.f;
            if (has_obj) { obj = MaybeSigmoid(at(idx, a)); ++idx; }

            float best = -1.f;
            int best_c = 0;
            for (int c = 0; c < nc; ++c) {
                const float s = MaybeSigmoid(at(idx + c, a));
                if (s > best) { best = s; best_c = c; }
            }
            idx += nc;
            d.score = obj * best;
            d.class_id = best_c;

            for (int k = 0; k < nk; ++k) {
                const float x = at(idx + 3 * k, a);
                const float y = at(idx + 3 * k + 1, a);
                const float s = MaybeSigmoid(at(idx + 3 * k + 2, a));
                d.kpts.push_back(Keypoint{ to_px(x), to_px(y), s });
            }
        }

        d.box = RectF{ to_px(cx - w * 0.5f), to_px(cy - h * 0.5f), to_px(w), to_px(h) };
        if (d.score < spec.conf_threshold) continue; // conf 内联过滤
        dets.push_back(std::move(d));
    }
    return dets;
}

void ScaleToScreen(std::vector<Detection>& dets,
                   float model_w, float model_h,
                   float screen_w, float screen_h) {
    if (model_w <= 0.f || model_h <= 0.f) return;
    const float sx = screen_w / model_w;
    const float sy = screen_h / model_h;
    for (auto& d : dets) {
        d.box.x *= sx; d.box.w *= sx;
        d.box.y *= sy; d.box.h *= sy;
        for (auto& k : d.kpts) { k.x *= sx; k.y *= sy; }
    }
}

void ScaleRoiToScreen(std::vector<Detection>& dets,
                      float model_w, float model_h,
                      float roi_x, float roi_y,
                      float roi_w, float roi_h) {
    if (model_w <= 0.f || model_h <= 0.f) return;
    if (roi_w <= 0.f || roi_h <= 0.f) return;
    const float sx = roi_w / model_w;
    const float sy = roi_h / model_h;
    for (auto& d : dets) {
        d.box.x = roi_x + d.box.x * sx;
        d.box.y = roi_y + d.box.y * sy;
        d.box.w *= sx;
        d.box.h *= sy;
        for (auto& k : d.kpts) {
            k.x = roi_x + k.x * sx;
            k.y = roi_y + k.y * sy;
        }
    }
}

} // namespace aim
