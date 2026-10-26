#pragma once
#include <string>
#include "core/spi.h"

// GHUB 虚拟 HID 执行通道（§5.6.2）：4 字节报告 [0, dx, dy, 0]，±127 分片。
//
// 通道语义（2026-10-26 实机验证）：
//   虚拟 HID 设备**不接受**标准 WriteFile（返回 ERROR_INVALID_FUNCTION=1），
//   必须用 DeviceIoControl(handle, 0x2A2010, [dx,dy,0,0], 4, ...)。
//   该码值取自 gh.dll 位移函数（0x180001250）的栈传参，实测被设备接受；
//   前置条件仍是装 LGS 9.02（G HUB 实例不转发位移，仅 LGS 实例可用）。
namespace aim::actuator {

// 位移 IOCTL：DeviceType 0x2A / Function 0x804 / METHOD_BUFFERED
inline constexpr unsigned long kMoveIoctl = 0x002A2010UL;

class GhubActuator final : public spi::IActuator {
public:
    GhubActuator();
    ~GhubActuator() override;
    GhubActuator(const GhubActuator&) = delete;
    GhubActuator& operator=(const GhubActuator&) = delete;

    // device_path 形如 \??\ROOT#SYSTEM#0002#{GUID}。
    // CreateFile 可直接打开（无需 NtCreateFile），但写入必须走 DeviceIoControl。
    bool init(const std::string& device_path, std::string* err = nullptr);
    // §4.4 SPI：按配置打开 GHUB 设备（actuator.ghub）
    bool configure(const ActuatorConfig& cfg) override;
    void close();
    bool is_open() const { return handle_ != nullptr; }

    void move(const spi::MoveCmd& cmd) override;
    void click(const spi::ClickCmd& cmd) override;

private:
    void* handle_ = nullptr;
    bool ioctl_mode_ = true; // true=DeviceIoControl；失败自动退回 WriteFile
};

} // namespace aim::actuator
