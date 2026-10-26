#include "aim_test.h"
#include "opsec/redact.h"

using namespace aim::opsec;

// Windows 绝对路径：只留文件名
AIM_TEST(scrub_windows_path) {
    SetPathScrubbing(true);
    AIM_EXPECT_STREQ(ScrubPaths("load C:\\Users\\bob\\proj\\data\\patterns.json done").c_str(),
                     "load …\\patterns.json done");
    AIM_EXPECT_STREQ(ScrubPaths("D:/MyProject/Test/x.onnx").c_str(), "…\\x.onnx");
}

// UNC 路径
AIM_TEST(scrub_unc_path) {
    SetPathScrubbing(true);
    AIM_EXPECT_STREQ(ScrubPaths("\\\\server\\share\\a\\b.txt").c_str(), "…\\b.txt");
}

// POSIX 绝对路径；相对片段不能被误伤
AIM_TEST(scrub_posix_and_relative) {
    SetPathScrubbing(true);
    AIM_EXPECT_STREQ(ScrubPaths("open /home/user/model.onnx").c_str(), "open …\\model.onnx");
    // 相对路径不是绝对路径，保持原样
    AIM_EXPECT_STREQ(ScrubPaths("data/patterns.json").c_str(), "data/patterns.json");
    AIM_EXPECT_STREQ(ScrubPaths("a/b/c").c_str(), "a/b/c");
}

// 无路径文本原样返回
AIM_TEST(scrub_leaves_plain_text) {
    SetPathScrubbing(true);
    AIM_EXPECT_STREQ(ScrubPaths("类别: 9 个；瞄准类别 = 身体, 头").c_str(),
                     "类别: 9 个；瞄准类别 = 身体, 头");
    AIM_EXPECT_STREQ(ScrubPaths("").c_str(), "");
}

// 一行里多个路径
AIM_TEST(scrub_multiple_paths) {
    SetPathScrubbing(true);
    AIM_EXPECT_STREQ(ScrubPaths("C:\\a\\x.json -> D:\\b\\y.json").c_str(),
                     "…\\x.json -> …\\y.json");
}

// 开关关闭时完全不动
AIM_TEST(scrub_disabled) {
    SetPathScrubbing(false);
    AIM_EXPECT_FALSE(PathScrubbingEnabled());
    AIM_EXPECT_STREQ(ScrubPaths("C:\\Users\\bob\\a.onnx").c_str(), "C:\\Users\\bob\\a.onnx");
    SetPathScrubbing(true); // 复原，避免影响其它用例
    AIM_EXPECT_TRUE(PathScrubbingEnabled());
}

// 引号/括号内的路径也要脱敏（日志常见形态）
AIM_TEST(scrub_inside_quotes) {
    SetPathScrubbing(true);
    AIM_EXPECT_STREQ(ScrubPaths("path=\"C:\\x\\y.onnx\"").c_str(), "path=\"…\\y.onnx\"");
    AIM_EXPECT_STREQ(ScrubPaths("（D:\\MyProject\\a\\b.json）").c_str(), "（…\\b.json）");
}

int main() {
    return aim::test::run_all();
}
