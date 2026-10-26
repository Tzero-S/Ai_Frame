#include "core/identity.h"

#include <cstdio>
#include <random>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace aim::identity {

namespace {
constexpr const char* kDiscoveryName = "aimframe.pipe";
}

std::string MakeEphemeralPipeName() {
    static const char* kHex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen((static_cast<uint64_t>(rd()) << 32) ^ rd());
    std::uniform_int_distribution<int> dist(0, 15);

    // 10 位十六进制 = 40 bit，碰撞概率可忽略；不带任何可读前缀
    std::string name;
    name.reserve(10);
    for (int i = 0; i < 10; ++i) name.push_back(kHex[dist(gen)]);
    return name;
}

std::string DiscoveryFilePath() {
#ifdef _WIN32
    char tmp[MAX_PATH]{};
    const DWORD n = GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) return {};
    std::string path(tmp);
    if (!path.empty() && path.back() != '\\' && path.back() != '/') path.push_back('\\');
    path += kDiscoveryName;
    return path;
#else
    return {};
#endif
}

std::string WriteDiscovery(const std::string& pipe_name) {
    const std::string path = DiscoveryFilePath();
    if (path.empty() || pipe_name.empty()) return {};
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return {};
    std::fwrite(pipe_name.data(), 1, pipe_name.size(), f);
    std::fclose(f);
    return path;
}

std::string ReadDiscovery() {
    const std::string path = DiscoveryFilePath();
    if (path.empty()) return {};
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    char buf[128]{};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    std::string name(buf, n);
    // 去掉可能的空白/换行
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ')) {
        name.pop_back();
    }
    return name;
}

void RemoveDiscovery() {
    const std::string path = DiscoveryFilePath();
    if (!path.empty()) std::remove(path.c_str());
}

} // namespace aim::identity
