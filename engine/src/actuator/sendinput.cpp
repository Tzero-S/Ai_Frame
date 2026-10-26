#include "actuator/sendinput.h"
#include <cmath>
#include <cstdlib>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace aim {

void ComputeMoveCounts(float dx_px, float dy_px, const SendInputParams& p,
                       int& out_dx, int& out_dy) {
    int dx = static_cast<int>(std::lround(dx_px * p.move_scale));
    int dy = static_cast<int>(std::lround(dy_px * p.move_scale));

    // 最小步长过滤：小于阈值的微动置零，避免抖动（§5.6.2）
    if (std::abs(dx) < p.min_step) dx = 0;
    if (std::abs(dy) < p.min_step) dy = 0;

    out_dx = dx;
    out_dy = dy;
}

SendInputActuator::SendInputActuator(const SendInputParams& p) : p_(p) {}

bool SendInputActuator::configure(const ActuatorConfig& cfg) {
    p_.move_scale = cfg.sendinput.move_scale;
    p_.min_step = cfg.sendinput.min_step;
    p_.absolute = cfg.sendinput.absolute;
    return true; // SendInput 无需打开设备
}

void SendInputActuator::move(const spi::MoveCmd& cmd) {
    int dx = 0, dy = 0;
    ComputeMoveCounts(cmd.dx_px, cmd.dy_px, p_, dx, dy);
    if (dx == 0 && dy == 0) return;

#ifdef _WIN32
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
#else
    (void)dx; (void)dy;
#endif
}

void SendInputActuator::click(const spi::ClickCmd& cmd) {
#ifdef _WIN32
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = cmd.down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(INPUT));
#else
    (void)cmd;
#endif
}

} // namespace aim
