#include "capture/capture_stub.h"
#include "capture/dxgi_capture.h"
#include "capture/gdi_capture.h"
#include "capture/scrcpy_capture.h"
#include "core/registry.h"
#include <memory>

namespace aim::capture {

void RegisterCaptureFactories() {
    Registry::register_factory<spi::ICapture>("dxgi", [] {
        return std::make_unique<DxgiCapture>();
    });
    Registry::register_factory<spi::ICapture>("gdi", [] {
        return std::make_unique<GdiCapture>();
    });
    Registry::register_factory<spi::ICapture>("scrcpy", [] {
        return std::make_unique<ScrcpyCapture>();
    });
}

} // namespace aim::capture
