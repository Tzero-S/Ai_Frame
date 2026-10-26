#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "actuator/bezier.h"
#include "actuator/kmbox.h"
#include "core/spi.h"

// KMBox 执行通道（§5.6.2 / §8.3）：UDP 72B 报文。
namespace aim::actuator {

class KmboxActuator final : public spi::IActuator {
public:
    KmboxActuator();
    ~KmboxActuator() override;
    KmboxActuator(const KmboxActuator&) = delete;
    KmboxActuator& operator=(const KmboxActuator&) = delete;

    bool init(const std::string& ip, int port, const std::string& uid, std::string* err = nullptr);
    // §4.4 SPI：按配置打开通道（KMBox 用 actuator.kmbox）
    bool configure(const ActuatorConfig& cfg) override;
    void close();
    bool ready() const { return ready_; }
    uint64_t packets_sent() const { return packets_; }

    void move(const spi::MoveCmd& cmd) override;
    void click(const spi::ClickCmd& cmd) override;

    bool send_connect();
    bool send_bezier(const KmboxMove& mv);

private:
    bool send_packet(KmboxCmd cmd, const KmboxMove& mv);

    uintptr_t sock_ = static_cast<uintptr_t>(~static_cast<uintptr_t>(0));
    uint32_t addr_be_ = 0;
    uint16_t port_be_ = 0;
    std::array<uint8_t, 4> uid_{};
    std::array<uint8_t, 6> mac_{};
    uint32_t rand_ = 1;
    uint16_t index_ = 0;
    uint64_t packets_ = 0;
    bool ready_ = false;
    // 贝塞尔路径（§5.6.3）：move() 在 cmd.bezier 为真时按曲线分片下发
    BezierSpec bezier_{};
    bool bezier_enabled_ = false;
};

} // namespace aim::actuator
