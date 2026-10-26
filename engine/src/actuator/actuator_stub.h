#pragma once
#include "core/spi.h"

// 执行通道注册（§4.4）。sendinput/ghub/kmbox/serial 均为真实实现类；
// 设备未接入时 init 失败并返回可执行错误信息（不静默失败）。
namespace aim::actuator {

// 空执行通道：只计数不下发。用于 --run --dry-run 与无硬件测试（不移动真实鼠标）。
class NullActuator final : public spi::IActuator {
public:
    void move(const spi::MoveCmd&) override { ++moves; }
    void click(const spi::ClickCmd&) override { ++clicks; }
    int moves = 0;
    int clicks = 0;
};

void RegisterActuatorFactories();

} // namespace aim::actuator
