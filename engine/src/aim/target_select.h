#pragma once
#include <vector>
#include "core/types.h"
#include "core/config.h"
#include "aim/scope.h"
#include "fov/fov_math.h"

// 目标选择（§5.5.1）。先按范围过滤，再按策略挑选。
namespace aim {

// 返回选中目标的指针（指向 dets 内部元素）；无合格目标返回 nullptr。
// locked 为上一帧锁定目标（仅 Sticky 使用；按指针身份判定其是否仍在候选集中）。
const Detection* SelectTarget(const std::vector<Detection>& dets,
                              TargetSelection mode,
                              const ScopeSpec& scope,
                              float screen_w, float screen_h,
                              const Fov& fov,
                              const Detection* locked = nullptr,
                              const Vec2F* center = nullptr);

} // namespace aim
