#include "capture/roi.h"
#include <algorithm>

namespace aim {

RoiRect ComputeRoi(RoiMode mode, int edge, int screen_w, int screen_h,
                   int client_origin_x, int client_origin_y) {
    RoiRect r;

    if (mode == RoiMode::Full) {
        r.x = client_origin_x;
        r.y = client_origin_y;
        r.w = std::max(0, screen_w);
        r.h = std::max(0, screen_h);
        return r;
    }

    // center_square：边长 edge，居中；超出屏幕时收缩到屏幕尺寸
    int e = (edge > 0) ? edge : 640;
    e = std::min(e, std::max(0, screen_w));
    e = std::min(e, std::max(0, screen_h));

    r.w = e;
    r.h = e;
    r.x = client_origin_x + (screen_w - e) / 2;
    r.y = client_origin_y + (screen_h - e) / 2;
    return r;
}

} // namespace aim
