#pragma once
#include <string>
#include <vector>
#include "core/types.h"

// 类别白名单过滤（§5.4 / FR-MDL-04）。白名单为空表示不过滤。
namespace aim {

void FilterClasses(std::vector<Detection>& dets, const std::vector<int>& whitelist);

// 按"类别名"解析白名单（schema v4）。
// 背景：类别索引跨模型会错位（同一个 0 在不同模型里可能是 body / 敌人 / 队友），
// 所以配置存名字，加载模型时再映射成索引。
struct ClassSelection {
    std::vector<int> indices;          // 模型内类别索引
    std::vector<std::string> names;    // 命中的类别名（与 indices 同序）
    bool auto_selected = false;        // 配置未命中，退回自动挑选
    std::string warning;               // 需要提示给用户的信息（可为空）
};

// 解析顺序：
//   1) configured 非空 → 先精确匹配，再大小写不敏感匹配；
//   2) 一个都没命中 → 回退到"自动挑选瞄准类"（body/head/敌人…，排除队友/倒地/靶场…）；
//   3) 自动挑选也失败 → 返回空（= 不过滤，全部类别）。
ClassSelection ResolveClassWhitelist(const std::vector<std::string>& configured,
                                     const std::vector<std::string>& model_classes);

} // namespace aim
