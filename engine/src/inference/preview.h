#pragma once
#include <string>
#include <vector>
#include "core/spi.h"

// 模型预览（§5.10.2）：与生产共用同一 IBackend 与后处理路径，
// 避免"预览能跑、实战不跑"的差异。
namespace aim::inference {

struct PreviewOptions {
    float conf_threshold = 0.35f;
    float iou_threshold = 0.45f;
    std::vector<int> class_whitelist;
};

struct PreviewResult {
    bool ok = false;
    std::string error;
    std::vector<Detection> detections; // 已做 conf/类别/NMS，模型输入坐标
    int raw_count = 0;
    spi::PerfCounters counters;
};

PreviewResult PreviewFrame(spi::IBackend& backend, const spi::Frame& frame,
                           const PreviewOptions& opt);

// [{"class_id":0,"score":0.83,"x":..,"y":..,"w":..,"h":..}]
std::string DetectionsToJson(const std::vector<Detection>& dets);

} // namespace aim::inference
