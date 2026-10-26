#pragma once
#include <vector>
#include "core/types.h"

// NMS（§7.6）：按 score 降序，保留最高分框，移除与其 IoU > 阈值的框。
// IoU 计算前先做 min/max 归一化（避免模型输出 x1>x2）。
namespace aim {

float Iou(const RectF& a, const RectF& b);

// 抑制范围。
//   PerClass —— 只在同类之间抑制（YOLO 官方口径）。body 与 head 天然重叠，
//               类别无关抑制会把其中一个吃掉，因此生产路径默认按类别。
//   Agnostic —— 跨类别抑制（旧行为，仅在明确需要"每处只留一个框"时使用）。
enum class NmsMode {
    PerClass,
    Agnostic,
};

// 原地执行贪心 NMS。默认阈值 0.45（§5.4），默认按类别抑制。
void Nms(std::vector<Detection>& dets, float iou_threshold = 0.45f,
         NmsMode mode = NmsMode::PerClass);

} // namespace aim
