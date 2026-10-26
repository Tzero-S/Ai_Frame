#include "inference/preview.h"
#include "core/json.h"
#include "postprocess/class_filter.h"
#include "postprocess/nms.h"

#include <algorithm>

namespace aim::inference {

PreviewResult PreviewFrame(spi::IBackend& backend, const spi::Frame& frame,
                           const PreviewOptions& opt) {
    PreviewResult r;

    std::vector<Detection> raw;
    if (!backend.infer(frame, raw)) {
        r.error = "infer failed";
        return r;
    }
    r.raw_count = static_cast<int>(raw.size());

    raw.erase(std::remove_if(raw.begin(), raw.end(),
                             [&](const Detection& d) { return d.score < opt.conf_threshold; }),
              raw.end());
    FilterClasses(raw, opt.class_whitelist);
    Nms(raw, opt.iou_threshold);

    r.detections = std::move(raw);
    r.counters = backend.counters();
    r.ok = true;
    return r;
}

std::string DetectionsToJson(const std::vector<Detection>& dets) {
    json::Value arr;
    for (const auto& d : dets) {
        json::Value v;
        v["class_id"] = d.class_id;
        v["score"] = d.score;
        v["x"] = d.box.x;
        v["y"] = d.box.y;
        v["w"] = d.box.w;
        v["h"] = d.box.h;
        arr.push_back(v);
    }
    return json::dump(arr, 0);
}

} // namespace aim::inference
