#include "actuator/actuator_stub.h"
#include "actuator/ghub_actuator.h"
#include "actuator/kmbox_actuator.h"
#include "actuator/sendinput.h"
#include "actuator/serial_actuator.h"
#include "core/registry.h"
#include "core/spi.h"
#include <memory>

namespace aim::actuator {

void RegisterActuatorFactories() {
    Registry::register_factory<spi::IActuator>("sendinput", [] {
        return std::make_unique<SendInputActuator>();
    });
    Registry::register_factory<spi::IActuator>("ghub", [] {
        return std::make_unique<GhubActuator>();
    });
    Registry::register_factory<spi::IActuator>("kmbox", [] {
        return std::make_unique<KmboxActuator>();
    });
    Registry::register_factory<spi::IActuator>("serial", [] {
        return std::make_unique<SerialActuator>();
    });
    // 调试/干跑通道：不触碰真实输入设备（§4.4 之外的扩展名，语义同覆盖层的 "none"）
    Registry::register_factory<spi::IActuator>("null", [] {
        return std::make_unique<NullActuator>();
    });
}

} // namespace aim::actuator
