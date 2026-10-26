#pragma once
#include <string>
#include <vector>
#include "core/types.h"

// YOLO-like 输出解码（§7.3，轴自适应）。
// 输出维度 [1,F,A] 或 [1,A,F]，F = 4(+1)+nc(+3*nkpt)。
// 解码结果为"模型输入像素空间"(0..imgsz)；屏幕映射见 ScaleToScreen（§5.4）。
namespace aim {

struct YoloOutputSpec {
    int num_classes = 1;
    int num_kpts = 0;
    int imgsz = 640;
    float conf_threshold = 0.35f;
    // has_obj：是否含 objectness（F=5+nc+3*nkpt）。默认按维度自动推断。
    bool has_obj = false;
    bool auto_detect_obj = true;
};

struct TensorView {
    const float* data = nullptr;
    int dims[4] = {1, 1, 1, 1};
    int ndim = 3;
};

// 解码。失败（维度非法/空数据）返回空并写 err。
std::vector<Detection> DecodeYolo(const TensorView& out, const YoloOutputSpec& spec,
                                  std::string* err = nullptr);

// §5.4 坐标映射：模型输入像素 → 屏幕像素（box 与 kpts 同时缩放）。
void ScaleToScreen(std::vector<Detection>& dets,
                   float model_w, float model_h,
                   float screen_w, float screen_h);

// §5.4 / §5.2.2 带 ROI 的坐标映射：模型输入像素 → 屏幕像素。
// 采集是屏幕中心的 roi_w×roi_h 裁剪，模型输入是该 ROI 的 imgsz×imgsz 重采样，
// 因此正确映射为 screen = roi_origin + model * roi_size / model_size。
// 仅在"模型输入即整屏"的简化场景才可退化为 ScaleToScreen。
void ScaleRoiToScreen(std::vector<Detection>& dets,
                      float model_w, float model_h,
                      float roi_x, float roi_y,
                      float roi_w, float roi_h);

} // namespace aim
