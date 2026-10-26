#pragma once
#include "core/config_store.h"
#include "core/runtime.h"
#include "ipc/handlers.h"

// 引擎内置 IPC 命令（§8.1）：engine.hello / config.get / config.apply /
// status.snapshot / model.import / model.preview。
// --serve（仅控制面）与 --run（数据面 + 控制面）共用同一套实现，避免两处漂移。
namespace aim::ipc {

void RegisterBuiltinHandlers(HandlerRegistry& reg, ConfigStore& store, const EngineRuntime& rt);

} // namespace aim::ipc
