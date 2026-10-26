#include "core/builtins.h"
#include "capture/capture_stub.h"
#include "inference/backend_stub.h"
#include "actuator/actuator_stub.h"
#include "overlay/overlay_stub.h"

namespace aim {

void RegisterBuiltinExtensions() {
    capture::RegisterCaptureFactories();
    inference::RegisterBackendFactories();
    actuator::RegisterActuatorFactories();
    overlay::RegisterOverlayFactories();
}

} // namespace aim
