#include "actuator/serial_actuator.h"

#include <windows.h>
#include <setupapi.h>

#include <cmath>
#include <cstdio>

namespace aim::actuator {

namespace {
// GUID_DEVCLASS_PORTS（避免依赖 uuid 库）
const GUID kPortsClassGuid = {
    0x4d36e978, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
} // namespace

std::vector<std::string> EnumerateSerialPorts() {
    std::vector<std::string> out;

    HDEVINFO di = SetupDiGetClassDevsA(&kPortsClassGuid, nullptr, nullptr, DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;

    SP_DEVINFO_DATA dd{};
    dd.cbSize = sizeof(dd);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); ++i) {
        HKEY key = SetupDiOpenDevRegKey(di, &dd, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) continue;

        char buf[256]{};
        DWORD len = sizeof(buf);
        DWORD type = 0;
        if (RegQueryValueExA(key, "PortName", nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf), &len) == ERROR_SUCCESS) {
            out.push_back(buf);
        }
        RegCloseKey(key);
    }
    SetupDiDestroyDeviceInfoList(di);
    return out;
}

SerialActuator::SerialActuator() = default;
SerialActuator::~SerialActuator() { close(); }

bool SerialActuator::configure(const ActuatorConfig& cfg) {
    return init(cfg.serial.port, cfg.serial.baud);
}

bool SerialActuator::init(const std::string& port, int baud, std::string* err) {
    close();
    const std::string path = "\\\\.\\" + port;

    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = "open serial failed: " + port;
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        if (err) *err = "GetCommState failed";
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(baud > 0 ? baud : 115200);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        if (err) *err = "SetCommState failed";
        return false;
    }

    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 10;
    to.WriteTotalTimeoutConstant = 100;
    SetCommTimeouts(h, &to);

    handle_ = h;
    return true;
}

void SerialActuator::close() {
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

void SerialActuator::move(const spi::MoveCmd& cmd) {
    if (!handle_) return;
    // 非有限值（NaN/Inf）直接丢弃：lround 对它们是未定义行为，
    // 而串口另一端拿到垃圾数值可能把光标甩飞。
    if (!std::isfinite(cmd.dx_px) || !std::isfinite(cmd.dy_px)) return;

    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "%d,%d\n",
                                static_cast<int>(std::lround(cmd.dx_px)),
                                static_cast<int>(std::lround(cmd.dy_px)));
    // snprintf 返回负值是编码错误，返回值 >= sizeof(buf) 是截断 —— 两种都不能照发
    if (n <= 0 || n >= static_cast<int>(sizeof(buf))) return;

    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(handle_), buf, static_cast<DWORD>(n), &written, nullptr);
}

void SerialActuator::click(const spi::ClickCmd& cmd) {
    if (!handle_) return;
    const char* s = cmd.down ? "1\n" : "0\n";
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(handle_), s, 2, &written, nullptr);
}

} // namespace aim::actuator
