#include "core/identity.h"

#include <cstdio>
#include <random>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
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
#ifdef _WIN32
    // 发现文件把"管道名"暴露给读它的人。%TEMP% 虽然按用户隔离，但同机的其他账户
    // 若拿到路径仍可能写入伪造值（把控制台引向攻击者的管道），所以这里显式收紧 DACL：
    // 只允许对象所有者（当前用户）访问。
    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa{};
    if (ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:P(A;;GA;;;OW)", SDDL_REVISION_1, &sd, nullptr)) {
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd;
        sa.bInheritHandle = FALSE;
    }
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, sd ? &sa : nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (sd) LocalFree(sd);
    if (h == INVALID_HANDLE_VALUE) return {};
    DWORD written = 0;
    const BOOL ok = WriteFile(h, pipe_name.data(), static_cast<DWORD>(pipe_name.size()),
                              &written, nullptr);
    CloseHandle(h);
    if (!ok || written != static_cast<DWORD>(pipe_name.size())) return {};
    return path;
#else
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return {};
    std::fwrite(pipe_name.data(), 1, pipe_name.size(), f);
    std::fclose(f);
    return path;
#endif
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
