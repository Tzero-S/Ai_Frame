#include "actuator/ghub_actuator.h"
#include "actuator/ghub.h"

#include <windows.h>

#include <cmath>

namespace aim::actuator {

GhubActuator::GhubActuator() = default;
GhubActuator::~GhubActuator() { close(); }

bool GhubActuator::configure(const ActuatorConfig& cfg) {
    return init(cfg.ghub.device_path);
}

bool GhubActuator::init(const std::string& device_path, std::string* err) {
    close();
    HANDLE h = CreateFileA(device_path.c_str(), GENERIC_WRITE | GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) {
            *err = "open GHUB device failed (err=" + std::to_string(GetLastError()) +
                   "): " + device_path;
        }
        return false;
    }
    handle_ = h;
    return true;
}

void GhubActuator::close() {
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

void GhubActuator::move(const spi::MoveCmd& cmd) {
    if (!handle_) return;

    const auto reports = GhubSplit(static_cast<int>(std::lround(cmd.dx_px)),
                                   static_cast<int>(std::lround(cmd.dy_px)), 127);
    HANDLE h = static_cast<HANDLE>(handle_);
    for (const auto& r : reports) {
        if (ioctl_mode_) {
            // 虚拟 HID 驱动不接受标准 WriteFile（ERROR_INVALID_FUNCTION），
            // 必须走 DeviceIoControl。码值来自 gh.dll 反汇编，已实测被设备接受。
            const unsigned char in[4] = {
                static_cast<unsigned char>(r.dx),
                static_cast<unsigned char>(r.dy),
                0, 0,
            };
            DWORD ret = 0;
            if (!DeviceIoControl(h, kMoveIoctl, const_cast<unsigned char*>(in),
                                 sizeof(in), nullptr, 0, &ret, nullptr)) {
                ioctl_mode_ = false; // 退回 WriteFile，避免每帧无效系统调用
            }
        } else {
            DWORD written = 0;
            WriteFile(h, &r, 4, &written, nullptr);
        }
    }
}

void GhubActuator::click(const spi::ClickCmd&) {
    // GHUB 按键报告需设备手册；当前通道仅负责相对位移（§5.6.2）。
}

} // namespace aim::actuator
