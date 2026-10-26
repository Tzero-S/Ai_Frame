#include "fov/fov_estimator.h"
#include <algorithm>
#include <cmath>

namespace aim {

FovEstimate EstimateFov(int screen_w, int screen_h, const FovConfig& cfg) {
    FovEstimate e;
    if (screen_w <= 0 || screen_h <= 0) {
        e.fov = Fov{ cfg.hfov_deg, cfg.vfov_deg };
        return e;
    }

    const float aspect = static_cast<float>(screen_w) / static_cast<float>(screen_h);
    e.auto_hfov_deg = HfovFromAspect(aspect);
    e.auto_vfov_deg = VfovFromHfov(e.auto_hfov_deg, aspect);

    if (cfg.manual_override) {
        e.manual = true;
        e.fov = Fov{ cfg.hfov_deg, cfg.vfov_deg };
        // 一致性提示：偏差 > 5%
        const float dh = std::fabs(cfg.hfov_deg - e.auto_hfov_deg) / e.auto_hfov_deg;
        e.inconsistent = dh > 0.05f;
    } else {
        e.manual = false;
        e.fov = Fov{ e.auto_hfov_deg, e.auto_vfov_deg };
    }
    // 开镜变焦（§5.8.2）：按当前倍率在表中插值，覆盖前面的结果。
    // 表为空或倍率为 1（腰射）时不生效，保持原有行为。
    if (cfg.ads_zoom > 1.0f && !cfg.ads_levels.empty()) {
        std::vector<FovConfig::AdsLevel> lv = cfg.ads_levels;
        std::sort(lv.begin(), lv.end(),
                  [](const FovConfig::AdsLevel& a, const FovConfig::AdsLevel& b) {
                      return a.zoom < b.zoom;
                  });
        float hfov = lv.back().hfov_deg;
        if (cfg.ads_zoom <= lv.front().zoom) {
            hfov = lv.front().hfov_deg;
        } else {
            for (size_t i = 1; i < lv.size(); ++i) {
                if (cfg.ads_zoom <= lv[i].zoom) {
                    const float span = lv[i].zoom - lv[i - 1].zoom;
                    const float t = (span > 1e-6f) ? (cfg.ads_zoom - lv[i - 1].zoom) / span : 0.f;
                    hfov = lv[i - 1].hfov_deg + t * (lv[i].hfov_deg - lv[i - 1].hfov_deg);
                    break;
                }
            }
        }
        e.fov = Fov{ hfov, VfovFromHfov(hfov, aspect) };
        e.ads_applied = true;
    }

    return e;
}

} // namespace aim
