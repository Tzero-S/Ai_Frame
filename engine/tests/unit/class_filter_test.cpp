#include "aim_test.h"
#include "postprocess/class_filter.h"

using namespace aim;

// 显式按名匹配：精确命中
AIM_TEST(class_resolve_exact) {
    const auto sel = ResolveClassWhitelist({"head"}, {"body", "head", "teammate"});
    AIM_EXPECT_EQ(sel.indices.size(), 1u);
    if (!sel.indices.empty()) AIM_EXPECT_EQ(sel.indices[0], 1);
    AIM_EXPECT_FALSE(sel.auto_selected);
    AIM_EXPECT_TRUE(sel.warning.empty());
}

// 大小写不敏感
AIM_TEST(class_resolve_case_insensitive) {
    const auto sel = ResolveClassWhitelist({"HEAD"}, {"Body", "Head"});
    AIM_EXPECT_EQ(sel.indices.size(), 1u);
    if (!sel.indices.empty()) AIM_EXPECT_EQ(sel.indices[0], 1);
}

// 多选 + 顺序按模型顺序去重
AIM_TEST(class_resolve_multi_dedup) {
    const auto sel = ResolveClassWhitelist({"head", "body", "head"}, {"body", "head"});
    AIM_EXPECT_EQ(sel.indices.size(), 2u);
    if (sel.indices.size() == 2) {
        AIM_EXPECT_EQ(sel.indices[0], 1); // head 先被解析
        AIM_EXPECT_EQ(sel.indices[1], 0);
    }
}

// 部分命中：命中的生效，未命中的给出告警
AIM_TEST(class_resolve_partial_warning) {
    const auto sel = ResolveClassWhitelist({"body", "不存在"}, {"body", "head"});
    AIM_EXPECT_EQ(sel.indices.size(), 1u);
    AIM_EXPECT_FALSE(sel.warning.empty());
}

// 全部未命中 → 自动挑选（排除队友/倒地）
AIM_TEST(class_auto_pick_excludes_teammate) {
    const auto sel = ResolveClassWhitelist({"whatever"}, {"body", "head", "teammate", "downed"});
    AIM_EXPECT_TRUE(sel.auto_selected);
    AIM_EXPECT_EQ(sel.indices.size(), 2u);
    if (sel.indices.size() == 2) {
        AIM_EXPECT_EQ(sel.indices[0], 0);
        AIM_EXPECT_EQ(sel.indices[1], 1);
    }
}

// 中文类别：身体/头 命中，队友/倒地/假人/靶场/闪光/大灯 排除
AIM_TEST(class_auto_pick_chinese) {
    const std::vector<std::string> sjz2 = {"身体", "头",   "队友",     "ai",   "倒地",
                                           "靶场头", "靶场身体", "闪光", "大灯"};
    const auto sel = ResolveClassWhitelist({}, sjz2);
    AIM_EXPECT_TRUE(sel.auto_selected);
    AIM_EXPECT_EQ(sel.indices.size(), 2u);
    if (sel.indices.size() == 2) {
        AIM_EXPECT_EQ(sel.indices[0], 0); // 身体
        AIM_EXPECT_EQ(sel.indices[1], 1); // 头
    }
}

// 模型没声明类别 → 空（不过滤），不误报
AIM_TEST(class_resolve_no_model_names) {
    const auto sel = ResolveClassWhitelist({"body"}, {});
    AIM_EXPECT_EQ(sel.indices.size(), 0u);
}

// 自动也挑不出来 → 空 = 不过滤，并给出提示
AIM_TEST(class_auto_pick_nothing) {
    const auto sel = ResolveClassWhitelist({}, {"alpha", "beta"});
    AIM_EXPECT_EQ(sel.indices.size(), 0u);
    AIM_EXPECT_FALSE(sel.warning.empty());
}

// FilterClasses：空白名单 = 全放行；非空则只留命中的
AIM_TEST(class_filter_keeps_whitelist) {
    std::vector<Detection> dets(3);
    dets[0].class_id = 0;
    dets[1].class_id = 1;
    dets[2].class_id = 2;

    auto all = dets;
    FilterClasses(all, {});
    AIM_EXPECT_EQ(all.size(), 3u);

    auto only_head = dets;
    FilterClasses(only_head, {1});
    AIM_EXPECT_EQ(only_head.size(), 1u);
    if (!only_head.empty()) AIM_EXPECT_EQ(only_head[0].class_id, 1);
}

int main() {
    return aim::test::run_all();
}
