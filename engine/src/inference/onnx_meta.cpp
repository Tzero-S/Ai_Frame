#include "inference/onnx_meta.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace aim::inference {

namespace {

bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsQuote(char c) { return c == '\'' || c == '"'; }
bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// 兜底：抓所有引号内字符串（老实现的行为，兼容非 dict 写法）
std::vector<std::string> FallbackQuotedStrings(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        if (IsQuote(s[i])) {
            const char quote = s[i++];
            std::string name;
            while (i < s.size() && s[i] != quote) name.push_back(s[i++]);
            if (i < s.size()) ++i;
            out.push_back(std::move(name));
        } else {
            ++i;
        }
    }
    return out;
}

} // namespace

std::vector<std::string> ParseClassNames(const std::string& names_meta) {
    std::vector<std::pair<long, std::string>> pairs;
    const size_t n = names_meta.size();
    size_t i = 0;

    while (i < n) {
        // 定位键的起点：数字，或带引号的数字
        bool key_quoted = false;
        if (IsQuote(names_meta[i])) {
            key_quoted = true;
            ++i;
        } else if (!IsDigit(names_meta[i])) {
            ++i;
            continue;
        }

        const size_t key_begin = i;
        while (i < n && IsDigit(names_meta[i])) ++i;
        if (i == key_begin) { // 引号里不是数字
            continue;
        }
        const long key = std::stol(names_meta.substr(key_begin, i - key_begin));

        if (key_quoted) {
            if (i < n && IsQuote(names_meta[i])) {
                ++i;
            } else {
                continue; // 引号没闭合，放弃这一处
            }
        }

        // 跳过空白与冒号，要求后面紧跟引号值
        while (i < n && (IsSpace(names_meta[i]) || names_meta[i] == ':')) ++i;
        if (i >= n || !IsQuote(names_meta[i])) continue;

        const char quote = names_meta[i++];
        std::string value;
        while (i < n && names_meta[i] != quote) {
            if (names_meta[i] == '\\' && i + 1 < n) {
                value.push_back(names_meta[i + 1]);
                i += 2;
                continue;
            }
            value.push_back(names_meta[i++]);
        }
        if (i < n) ++i; // 跳过收尾引号
        pairs.emplace_back(key, std::move(value));
    }

    if (pairs.empty()) return FallbackQuotedStrings(names_meta);

    std::stable_sort(pairs.begin(), pairs.end(),
                     [](const std::pair<long, std::string>& a,
                        const std::pair<long, std::string>& b) { return a.first < b.first; });

    std::vector<std::string> out;
    out.reserve(pairs.size());
    for (auto& p : pairs) out.push_back(std::move(p.second));
    return out;
}

} // namespace aim::inference
