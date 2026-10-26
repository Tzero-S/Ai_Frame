#pragma once
#include "core/spi.h"

// SendInput 执行通道（§5.6.2）：相对移动 SendInput(MOUSEEVENTF_MOVE)，
// 含 move_scale（灵敏度换算）与 min_step（最小步长，避免抖动）。
namespace aim {

struct SendInputParams {
    float move_scale = 1.0f;
    int min_step = 1;
    bool absolute = false;
};

// 纯逻辑：像素位移 → 鼠标计数（四舍五入 + 最小步长过滤）。
void ComputeMoveCounts(float dx_px, float dy_px, const SendInputParams& p,
                       int& out_dx, int& out_dy);

class SendInputActuator final : public spi::IActuator {
public:
    explicit SendInputActuator(const SendInputParams& p = {});
    // §4.4 SPI：按配置更新灵敏度换算/最小步长（actuator.sendinput）
    bool configure(const ActuatorConfig& cfg) override;
    void move(const spi::MoveCmd& cmd) override;
    void click(const spi::ClickCmd& cmd) override;

private:
    SendInputParams p_;
};

} // namespace aim
