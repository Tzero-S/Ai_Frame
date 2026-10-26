#pragma once

// 推理后端注册（§4.4 / §5.3.1）。dml / cpu 均指向 ONNX Runtime 后端实现；
// 实际 EP 在 OrtBackend 内部选择（CPU / DirectML）。
namespace aim::inference {

void RegisterBackendFactories();

} // namespace aim::inference
