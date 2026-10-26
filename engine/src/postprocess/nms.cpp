#include "postprocess/nms.h"
#include <algorithm>

namespace aim {

float Iou(const RectF& a, const RectF& b) {
    // min/max 归一化（§7.6）
    const float ax1 = std::min(a.x, a.right());
    const float ax2 = std::max(a.x, a.right());
    const float ay1 = std::min(a.y, a.bottom());
    const float ay2 = std::max(a.y, a.bottom());
    const float bx1 = std::min(b.x, b.right());
    const float bx2 = std::max(b.x, b.right());
    const float by1 = std::min(b.y, b.bottom());
    const float by2 = std::max(b.y, b.bottom());

    const float ix1 = std::max(ax1, bx1);
    const float iy1 = std::max(ay1, by1);
    const float ix2 = std::min(ax2, bx2);
    const float iy2 = std::min(ay2, by2);

    const float iw = std::max(0.f, ix2 - ix1);
    const float ih = std::max(0.f, iy2 - iy1);
    const float inter = iw * ih;

    const float area_a = (ax2 - ax1) * (ay2 - ay1);
    const float area_b = (bx2 - bx1) * (by2 - by1);
    const float uni = area_a + area_b - inter;
    return (uni > 0.f) ? (inter / uni) : 0.f;
}

void Nms(std::vector<Detection>& dets, float iou_threshold, NmsMode mode) {
    std::sort(dets.begin(), dets.end(),
              [](const Detection& a, const Detection& b) { return a.score > b.score; });

    std::vector<char> removed(dets.size(), 0);
    std::vector<Detection> kept;
    kept.reserve(dets.size());

    for (size_t i = 0; i < dets.size(); ++i) {
        if (removed[i]) continue;
        kept.push_back(dets[i]);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (removed[j]) continue;
            // 按类别抑制：不同类别的框互不压制（body/head 重叠是正常现象）
            if (mode == NmsMode::PerClass && dets[j].class_id != dets[i].class_id) continue;
            if (Iou(dets[i].box, dets[j].box) > iou_threshold) removed[j] = 1;
        }
    }
    dets.swap(kept);
}

} // namespace aim
