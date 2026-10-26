#include "aim_test.h"
#include "inference/onnx_meta.h"

using namespace aim::inference;

// Ultralytics 原生格式：整数键不带引号
AIM_TEST(meta_names_ultralytics_format) {
    const auto v = ParseClassNames("{0: 'body', 1: 'head'}");
    AIM_EXPECT_EQ(v.size(), 2u);
    if (v.size() == 2) {
        AIM_EXPECT_STREQ(v[0].c_str(), "body");
        AIM_EXPECT_STREQ(v[1].c_str(), "head");
    }
}

// 带引号的键（部分导出工具会这么写）——不能被当成类名
AIM_TEST(meta_names_quoted_keys) {
    const auto v = ParseClassNames("{\"0\": \"body\", \"1\": \"head\"}");
    AIM_EXPECT_EQ(v.size(), 2u);
    if (v.size() == 2) {
        AIM_EXPECT_STREQ(v[0].c_str(), "body");
        AIM_EXPECT_STREQ(v[1].c_str(), "head");
    }
}

// 键乱序：按 key 数值排序，而不是出现顺序
AIM_TEST(meta_names_sorted_by_key) {
    const auto v = ParseClassNames("{2: 'c', 0: 'a', 1: 'b'}");
    AIM_EXPECT_EQ(v.size(), 3u);
    if (v.size() == 3) {
        AIM_EXPECT_STREQ(v[0].c_str(), "a");
        AIM_EXPECT_STREQ(v[1].c_str(), "b");
        AIM_EXPECT_STREQ(v[2].c_str(), "c");
    }
}

// 中文类别（真实样本 sjz2 的元数据原文）
AIM_TEST(meta_names_chinese) {
    const auto v = ParseClassNames(
        "{0: '身体', 1: '头', 2: '队友', 3: 'ai', 4: '倒地', 5: '靶场头', 6: '靶场身体', "
        "7: '闪光', 8: '大灯'}");
    AIM_EXPECT_EQ(v.size(), 9u);
    if (v.size() == 9) {
        AIM_EXPECT_STREQ(v[0].c_str(), "身体");
        AIM_EXPECT_STREQ(v[1].c_str(), "头");
        AIM_EXPECT_STREQ(v[8].c_str(), "大灯");
    }
}

// 双引号值 + 转义
AIM_TEST(meta_names_double_quote_escape) {
    const auto v = ParseClassNames("{0: \"a\\\"b\", 1: 'c'}");
    AIM_EXPECT_EQ(v.size(), 2u);
    if (v.size() == 2) {
        AIM_EXPECT_STREQ(v[0].c_str(), "a\"b");
        AIM_EXPECT_STREQ(v[1].c_str(), "c");
    }
}

// 非 dict 写法：退化为抓所有引号内字符串
AIM_TEST(meta_names_fallback_list) {
    const auto v = ParseClassNames("['body', 'head']");
    AIM_EXPECT_EQ(v.size(), 2u);
    if (v.size() == 2) {
        AIM_EXPECT_STREQ(v[0].c_str(), "body");
    }
}

// 空/非法输入不崩
AIM_TEST(meta_names_empty_and_garbage) {
    AIM_EXPECT_EQ(ParseClassNames("").size(), 0u);
    AIM_EXPECT_EQ(ParseClassNames("{: }").size(), 0u);
    AIM_EXPECT_EQ(ParseClassNames("{0: }").size(), 0u);
}

int main() {
    return aim::test::run_all();
}
