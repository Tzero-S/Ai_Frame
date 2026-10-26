#include "postprocess/class_filter.h"
#include <algorithm>
#include <cctype>

namespace aim {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool Contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// 瞄准目标关键词：命中任一即视为"该瞄的"
bool LooksLikeTarget(const std::string& lower) {
    static const char* kAim[] = {"body", "head", "torso", "person", "enemy",
                                 "身体", "头", "敌人"};
    for (const char* w : kAim) {
        if (Contains(lower, w)) return true;
    }
    return false;
}

// 排除关键词：队友/倒地/假人/靶场/道具/闪光等不该瞄的
bool LooksLikeDecoy(const std::string& lower) {
    static const char* kNo[] = {"teammate", "friend", "dummy", "downed", "dead",
                                "breakable", "dodge", "flash", "light", "item",
                                "队友", "倒地", "假人", "靶场", "闪光", "大灯", "道具"};
    for (const char* w : kNo) {
        if (Contains(lower, w)) return true;
    }
    return false;
}

} // namespace

void FilterClasses(std::vector<Detection>& dets, const std::vector<int>& whitelist) {
    if (whitelist.empty()) return; // 空 = 全部类别
    dets.erase(std::remove_if(dets.begin(), dets.end(),
                              [&](const Detection& d) {
                                  return std::find(whitelist.begin(), whitelist.end(),
                                                   d.class_id) == whitelist.end();
                              }),
               dets.end());
}

ClassSelection ResolveClassWhitelist(const std::vector<std::string>& configured,
                                     const std::vector<std::string>& model_classes) {
    ClassSelection out;
    if (model_classes.empty()) {
        // 模型没声明类别：无从按名映射，交给"空 = 全部"
        if (!configured.empty()) out.warning = "模型未声明类别名，已忽略 selected_class_names";
        return out;
    }

    const int n = static_cast<int>(model_classes.size());

    // 1) 显式配置：精确 → 大小写不敏感
    if (!configured.empty()) {
        std::vector<std::string> missing;
        for (const auto& want : configured) {
            int hit = -1;
            for (int i = 0; i < n; ++i) {
                if (model_classes[static_cast<size_t>(i)] == want) {
                    hit = i;
                    break;
                }
            }
            if (hit < 0) {
                const std::string lw = Lower(want);
                for (int i = 0; i < n; ++i) {
                    if (Lower(model_classes[static_cast<size_t>(i)]) == lw) {
                        hit = i;
                        break;
                    }
                }
            }
            if (hit >= 0) {
                if (std::find(out.indices.begin(), out.indices.end(), hit) == out.indices.end()) {
                    out.indices.push_back(hit);
                    out.names.push_back(model_classes[static_cast<size_t>(hit)]);
                }
            } else {
                missing.push_back(want);
            }
        }
        if (!out.indices.empty()) {
            if (!missing.empty()) {
                out.warning = "以下类别在模型中不存在，已忽略: ";
                for (size_t i = 0; i < missing.size(); ++i) {
                    if (i) out.warning += ", ";
                    out.warning += missing[i];
                }
            }
            return out;
        }
        out.warning = "selected_class_names 与模型类别完全不匹配，回退到自动挑选";
    }

    // 2) 自动挑选：像目标、且不像诱饵
    for (int i = 0; i < n; ++i) {
        const std::string lower = Lower(model_classes[static_cast<size_t>(i)]);
        if (LooksLikeTarget(lower) && !LooksLikeDecoy(lower)) {
            out.indices.push_back(i);
            out.names.push_back(model_classes[static_cast<size_t>(i)]);
        }
    }
    out.auto_selected = true;

    // 3) 自动也挑不出来：返回空（不过滤）
    if (out.indices.empty()) {
        out.warning = "未能自动识别瞄准类别，已放开全部类别（请显式配置 selected_class_names）";
    }
    return out;
}

} // namespace aim
