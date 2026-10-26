#pragma once
#include <cstdint>

// 预处理（§5.3.1）：BGRA 采集帧 → 模型输入张量。
// 与 ONNX Runtime 解耦，便于在无 ORT 的构建与单元测试中直接验证。
namespace aim::inference {

// BGRA(8bit) → float32 NCHW RGB，归一化到 [0,1]。
// 最近邻缩放：目标尺寸与源尺寸不同时按比例取样（无 letterbox，保持既有口径）。
// 参数非法（空指针 / 非正尺寸）时直接返回，不写越界、不做除法。
void FillNchwRgb(const uint8_t* src, int sw, int sh,
                 float* dst, int dw, int dh);

// BGRA(8bit) → uint8 NCHW RGB（不归一化），用于 uint8 输入模型。
// 参数校验口径同 FillNchwRgb。
void FillNchwU8(const uint8_t* src, int sw, int sh,
                uint8_t* dst, int dw, int dh);

} // namespace aim::inference
