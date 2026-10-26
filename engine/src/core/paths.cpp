#include "core/paths.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace aim {

std::string ExeDir() {
#ifdef _WIN32
    char buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::string path(buf, n);
    const size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return {};
    return path.substr(0, slash);
#else
    return {};
#endif
}

namespace {
bool Exists(const std::string& p) {
    if (p.empty()) return false;
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}
} // namespace

std::string ResolveDataFile(const std::string& path) {
    if (path.empty()) return {};
    if (Exists(path)) return path;

    // 相对路径 → 相对可执行目录再试一次
    const bool absolute =
        path.size() > 1 && (path[0] == '/' || path[0] == '\\' ||
                            (path.size() > 2 && path[1] == ':'));
    if (absolute) return {};

    const std::string dir = ExeDir();
    if (dir.empty()) return {};
    const std::string joined = dir + "/" + path;
    if (Exists(joined)) return joined;
    return {};
}

} // namespace aim
