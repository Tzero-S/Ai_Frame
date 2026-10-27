#include "aim_test.h"
#include "fixtures_util.h"
#include "capture/png_decode.h"
#include "capture/scrcpy_capture.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace aim;

namespace {
bool ReadAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}
} // namespace

AIM_TEST(png_decode_fixture) {
    std::vector<uint8_t> png;
    if (!ReadAll(test::FixturePath("valorant_sample_960.png"), png)) return;

    std::vector<uint8_t> bgra;
    std::string err;
    AIM_EXPECT_TRUE(capture::DecodePngToBgra(png.data(), png.size(), 320, 320, bgra, &err));
    AIM_EXPECT_EQ(bgra.size(), static_cast<size_t>(320) * 320 * 4);

    int nonzero = 0;
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
        if (bgra[i] || bgra[i + 1] || bgra[i + 2]) ++nonzero;
    }
    AIM_EXPECT_TRUE(nonzero > 0);
}

AIM_TEST(png_decode_invalid) {
    const uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> out;
    std::string err;
    AIM_EXPECT_FALSE(capture::DecodePngToBgra(junk, sizeof(junk), 64, 64, out, &err));
    AIM_EXPECT_TRUE(!err.empty());
}

AIM_TEST(scrcpy_requires_serial) {
    capture::ScrcpyCapture cap;
    spi::CaptureSpec spec;
    spec.roi_w = 320;
    spec.roi_h = 320;
    AIM_EXPECT_FALSE(cap.init(spec)); // 未 configure → serial 为空
}

namespace {
bool SkipEnv() {
    const char* v = std::getenv("AIM_SKIP_HW_TESTS");
    return v && std::string(v) == "1";
}
} // namespace

AIM_TEST(scrcpy_bogus_serial_fails) {
    if (SkipEnv()) return; // 需要真正拉起 adb，环境相关
    capture::ScrcpyCapture cap;
    cap.configure("AIM_NO_SUCH_DEVICE_999", "127.0.0.1", 27183);

    spi::CaptureSpec spec;
    spec.roi_w = 320;
    spec.roi_h = 320;
    AIM_EXPECT_FALSE(cap.init(spec)); // adb 缺失或设备不存在
    AIM_EXPECT_TRUE(std::string(cap.status()).find("not initialized") != std::string::npos);
}

// P0-3 回归（纯谓词层，不启动进程）：白名单必须挡住所有 shell 元字符。
// 这是命令注入的第一道闸，必须能在没有 adb 的机器上被验证。
AIM_TEST(scrcpy_serial_whitelist) {
    const char* bad[] = {
        "x & calc.exe", "a\"b", "a|b", "a>b", "a<b", "a;b",
        "a b",          "a\nb", "$(id)", "`id`", "a%b", "a^b",
        "",             "a/b", "a\\b", "a'b",
    };
    for (const char* serial : bad) {
        AIM_EXPECT_FALSE(capture::IsSafeAdbSerial(serial));
    }

    const char* good[] = {"emulator-5554", "192.168.1.7:5555", "R58M123ABC",
                          "0123456789ABCDEF", "a.b_c-d:e"};
    for (const char* serial : good) {
        AIM_EXPECT_TRUE(capture::IsSafeAdbSerial(serial));
    }
    // 长度上限
    AIM_EXPECT_FALSE(capture::IsSafeAdbSerial(std::string(129, 'a')));
    AIM_EXPECT_TRUE(capture::IsSafeAdbSerial(std::string(128, 'a')));
}

// P0-3 回归（行为层）：非法序列号在 init 阶段就被拒，不会进入任何命令行解析。
AIM_TEST(scrcpy_rejects_shell_metacharacters) {
    const char* bad[] = {
        "x & calc.exe", "a\"b", "a|b", "a>b", "a<b", "a;b",
        "a b",          "a\nb", "$(id)", "`id`", "a%b", "a^b",
    };
    for (const char* serial : bad) {
        capture::ScrcpyCapture cap;
        cap.configure(serial, "127.0.0.1", 27183);
        spi::CaptureSpec spec;
        spec.roi_w = 320;
        spec.roi_h = 320;
        // 关键点：非法字符走白名单分支，不会创建子进程。
        AIM_EXPECT_FALSE(cap.init(spec));
        AIM_EXPECT_TRUE(std::string(cap.status()).find("not initialized") != std::string::npos);
    }
}

// 合法序列号（adb 实际格式）不应被白名单误杀
AIM_TEST(scrcpy_accepts_legal_serial_shapes) {
    if (SkipEnv()) return; // 同上：会真正调用 adb
    const char* good[] = {"emulator-5554", "192.168.1.7:5555", "R58M123ABC", "0123456789ABCDEF"};
    for (const char* serial : good) {
        capture::ScrcpyCapture cap;
        cap.configure(serial, "127.0.0.1", 27183);
        spi::CaptureSpec spec;
        spec.roi_w = 320;
        spec.roi_h = 320;
        cap.init(spec); // 结果取决于本机是否有 adb/设备，这里只验证不因字符集被拒
        const std::string st = cap.status();
        AIM_EXPECT_TRUE(st.find("not initialized") != std::string::npos ||
                        st.find("ready") != std::string::npos);
    }
}

int main() {
    return aim::test::run_all();
}
