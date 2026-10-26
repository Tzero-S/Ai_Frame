#include "actuator/dispatcher.h"

namespace aim {

bool Dispatcher::Dispatch(const spi::MoveCmd& cmd) {
    if (!actuator_) return false;

    // seq 校验：丢弃过期指令（§5.6.4）
    if (cmd.seq != 0 && cmd.seq <= last_seq_) {
        ++dropped_;
        return false;
    }
    if (cmd.seq != 0) last_seq_ = cmd.seq;

    actuator_->move(cmd);
    return true;
}

bool Dispatcher::DispatchClick(const spi::ClickCmd& cmd) {
    if (!actuator_) return false;
    actuator_->click(cmd);
    return true;
}

} // namespace aim
