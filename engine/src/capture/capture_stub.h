#pragma once

// 采集通道注册（§4.4 / §5.2）。dxgi / gdi / scrcpy 均已实装：
//   - dxgi_capture.h / gdi_capture.h / scrcpy_capture.h
namespace aim::capture {

void RegisterCaptureFactories();

} // namespace aim::capture
