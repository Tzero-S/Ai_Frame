#include "opsec/redact.h"

#include <atomic>
#include <cctype>

namespace aim::opsec {

namespace {

std::atomic<bool> g_enabled{true};

inline bool IsSep(char c) { return c == '\\' || c == '/'; }

// 路径在文本中的结束符：空白、引号、括号、逗号分号
inline bool IsDelim(char c) {
    return std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '\'' || c == ')' ||
           c == ']' || c == '}' || c == ',' || c == ';' || c == '>' || c == '<';
}

// 判断 text[i] 是否是一个路径的开头
bool IsPathStart(std::string_view text, size_t i) {
    const size_t n = text.size();
    // Windows 盘符：C:\ 或 C:/
    if (i + 2 < n && std::isalpha(static_cast<unsigned char>(text[i])) && text[i + 1] == ':' &&
        IsSep(text[i + 2])) {
        return true;
    }
    // UNC：\\server\share
    if (i + 2 < n && text[i] == '\\' && text[i + 1] == '\\' &&
        !IsDelim(text[i + 2])) {
        return true;
    }
    // POSIX 绝对路径：/a/b（要求前面不是路径字符，避免误伤 "a/b" 这种相对片段）
    if (text[i] == '/' && i + 1 < n && !IsDelim(text[i + 1]) && text[i + 1] != '/') {
        const bool prev_ok = (i == 0) || IsDelim(text[i - 1]) || text[i - 1] == '=' ||
                             text[i - 1] == '(';
        return prev_ok;
    }
    return false;
}

} // namespace

void SetPathScrubbing(bool on) { g_enabled.store(on, std::memory_order_relaxed); }
bool PathScrubbingEnabled() { return g_enabled.load(std::memory_order_relaxed); }

std::string ScrubPaths(std::string_view text) {
    if (!PathScrubbingEnabled() || text.empty()) return std::string(text);

    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (!IsPathStart(text, i)) {
            out.push_back(text[i++]);
            continue;
        }
        // 扫描到分隔符为止
        size_t j = i;
        while (j < text.size() && !IsDelim(text[j])) ++j;

        // 取最后一段作为文件名
        size_t base = j;
        while (base > i && !IsSep(text[base - 1])) --base;
        // 末尾若是分隔符（目录），退一格
        if (base >= j && j > i) base = j;

        out += "…";
        out.push_back('\\');
        out.append(text.substr(base, j - base));
        i = j;
    }
    return out;
}

} // namespace aim::opsec
