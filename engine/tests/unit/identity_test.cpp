#include "aim_test.h"
#include "core/identity.h"

#include <set>
#include <string>

using namespace aim::identity;

// 随机名格式：10 位小写十六进制，不含任何可读前缀
AIM_TEST(identity_pipe_name_format) {
    std::set<std::string> seen;
    for (int i = 0; i < 16; ++i) {
        const std::string n = MakeEphemeralPipeName();
        AIM_EXPECT_EQ(n.size(), 10u);
        for (char c : n) {
            const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            AIM_EXPECT_TRUE(hex);
        }
        seen.insert(n);
    }
    // 16 次里不应出现重复（40bit 空间）
    AIM_EXPECT_TRUE(seen.size() >= 15u);
}

// 发现文件：写 → 读 → 删
AIM_TEST(identity_discovery_roundtrip) {
    const std::string path = DiscoveryFilePath();
    AIM_EXPECT_FALSE(path.empty());

    const std::string name = MakeEphemeralPipeName();
    AIM_EXPECT_STREQ(WriteDiscovery(name).c_str(), path.c_str());
    AIM_EXPECT_STREQ(ReadDiscovery().c_str(), name.c_str());

    RemoveDiscovery();
    AIM_EXPECT_TRUE(ReadDiscovery().empty());
}

// 空名字不写文件；重复写覆盖
AIM_TEST(identity_discovery_edge_cases) {
    AIM_EXPECT_TRUE(WriteDiscovery("").empty());

    AIM_EXPECT_FALSE(WriteDiscovery("aaaa1111bb").empty());
    AIM_EXPECT_FALSE(WriteDiscovery("cccc2222dd").empty());
    AIM_EXPECT_STREQ(ReadDiscovery().c_str(), "cccc2222dd"); // 后写覆盖

    RemoveDiscovery();
    AIM_EXPECT_TRUE(ReadDiscovery().empty());
}

int main() {
    return aim::test::run_all();
}
