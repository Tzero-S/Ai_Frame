#pragma once

// §5.12.4 反调试：默认关闭，需显式开启（否则影响正常调试与客服排障）。
namespace aim::opsec {

bool IsDebuggerAttached();

// enabled=false 时恒为 false（放行）。
bool AntiDebugGate(bool enabled);

} // namespace aim::opsec
