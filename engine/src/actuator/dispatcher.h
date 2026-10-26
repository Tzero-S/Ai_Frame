#pragma once
#include <cstdint>
#include "core/spi.h"

// 执行分发（§5.6.4）：seq 校验 + 通道下发。
//   "下发前若 seq 已过期则丢弃（避免瞄准点已移动仍补发）"
namespace aim {

class Dispatcher {
public:
    void SetActuator(spi::IActuator* actuator) { actuator_ = actuator; }

    // seq==0 表示未编号（总是接受）；否则 seq <= 已下发序号时丢弃。
    bool Dispatch(const spi::MoveCmd& cmd);
    bool DispatchClick(const spi::ClickCmd& cmd);

    uint32_t NextSeq() { return ++seq_gen_; }
    uint32_t DroppedCount() const { return dropped_; }
    uint32_t LastSeq() const { return last_seq_; }

private:
    spi::IActuator* actuator_ = nullptr;
    uint32_t seq_gen_ = 0;
    uint32_t last_seq_ = 0;
    uint32_t dropped_ = 0;
};

} // namespace aim
