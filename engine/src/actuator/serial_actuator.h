#pragma once
#include <string>
#include <vector>
#include "core/spi.h"

// 串口执行通道（§5.6.2）：写 "dx,dy\n"（ESP32 下位机执行物理移动）。
namespace aim::actuator {

// 枚举本机串口（SetupAPI），返回如 {"COM1","COM3"}。
std::vector<std::string> EnumerateSerialPorts();

class SerialActuator final : public spi::IActuator {
public:
    SerialActuator();
    ~SerialActuator() override;
    SerialActuator(const SerialActuator&) = delete;
    SerialActuator& operator=(const SerialActuator&) = delete;

    bool init(const std::string& port, int baud, std::string* err = nullptr);
    // §4.4 SPI：按配置打开串口（actuator.serial）
    bool configure(const ActuatorConfig& cfg) override;
    void close();
    bool is_open() const { return handle_ != nullptr; }

    void move(const spi::MoveCmd& cmd) override;
    void click(const spi::ClickCmd& cmd) override;

private:
    void* handle_ = nullptr;
};

} // namespace aim::actuator
