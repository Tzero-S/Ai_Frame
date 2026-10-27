#include "core/json.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace aim::json {

namespace {

// 递归深度上限：解析器是递归下降，深度不设限时 20KB 的 `[[[[...` 就能打爆线程栈
// （实测 10000 层 → STATUS_STACK_OVERFLOW，进程直接终止、catch 不到）。
// 配置文件与 IPC 报文的实际嵌套不超过 10 层，128 足够宽松。
constexpr int kMaxParseDepth = 128;

struct Parser {
    const std::string& s;
    std::size_t i = 0;
    std::string& err;
    int depth = 0;
    Parser(const std::string& str, std::string& e) : s(str), err(e) {}

    [[noreturn]] void fail(const std::string& m) {
        err = m;
        throw std::runtime_error(m);
    }

    // 进入一层容器；析构时退出（异常安全）
    struct DepthGuard {
        Parser& p;
        explicit DepthGuard(Parser& parser) : p(parser) {
            if (++p.depth > kMaxParseDepth) p.fail("nesting too deep (max 128)");
        }
        ~DepthGuard() { --p.depth; }
    };
    void ws() {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
            else break;
        }
    }
    Value parseValue() {
        ws();
        if (i >= s.size()) fail("unexpected end of input");
        char c = s[i];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') { Value v; v.type = JType::String; v.str = parseString(); return v; }
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') { parseLiteral("null"); return Value{}; }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail("unexpected character");
        return Value{};
    }
    // 把码点按 UTF-8 追加（代理对已在调用处合并）
    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // 读 4 位十六进制（不含前导 \u）
    unsigned readHex4() {
        if (i + 4 > s.size()) fail("bad unicode escape");
        unsigned code = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s[i++];
            code <<= 4;
            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
            else fail("bad hex digit");
        }
        return code;
    }

    std::string parseString() {
        ++i; // opening quote
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= s.size()) fail("bad escape");
                char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned code = readHex4();
                        // UTF-16 代理对：\uD83D\uDE00 → 单个 U+1F600（4 字节 UTF-8）。
                        // 旧实现把高低代理各编成 3 字节，产出 CESU-8 乱码。
                        if (code >= 0xD800 && code <= 0xDBFF) {
                            if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                                i += 2;
                                const unsigned lo = readHex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                    const unsigned cp =
                                        0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
                                    AppendUtf8(out, cp);
                                    break;
                                }
                                // 高代理后面不是低代理：两个都按替换字符处理
                                AppendUtf8(out, 0xFFFD);
                                AppendUtf8(out, (lo >= 0xD800 && lo <= 0xDFFF) ? 0xFFFD : lo);
                                break;
                            }
                            AppendUtf8(out, 0xFFFD); // 孤立高代理
                            break;
                        }
                        if (code >= 0xDC00 && code <= 0xDFFF) {
                            AppendUtf8(out, 0xFFFD); // 孤立低代理
                            break;
                        }
                        AppendUtf8(out, code);
                        break;
                    }
                    default: fail("bad escape char");
                }
            } else {
                out += c;
            }
        }
        fail("unterminated string");
        return out;
    }
    // 严格按 JSON 数字文法扫描：-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?
    // 旧实现只要见到 . e E + - 就吞，`1-2`、`1e++5`、`--3` 都会被接受并被 strtod
    // 静默截断成别的值 —— 配置里写错一个字符会变成"看起来成功但数值不对"。
    Value parseNumber() {
        const std::size_t start = i;
        auto digits = [&]() {
            const std::size_t b = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            return i - b;
        };

        if (i < s.size() && s[i] == '-') ++i;

        if (i >= s.size()) fail("bad number");
        if (s[i] == '0') {
            ++i; // 前导 0 后不允许再跟数字（JSON 不允许 01）
            if (i < s.size() && s[i] >= '0' && s[i] <= '9') fail("leading zero in number");
        } else if (s[i] >= '1' && s[i] <= '9') {
            digits();
        } else {
            fail("bad number");
        }

        if (i < s.size() && s[i] == '.') {
            ++i;
            if (digits() == 0) fail("bad fraction");
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (digits() == 0) fail("bad exponent");
        }

        const std::string num = s.substr(start, i - start);
        Value v;
        v.type = JType::Number;
        v.num = std::strtod(num.c_str(), nullptr);
        return v;
    }
    Value parseBool() {
        if (s.compare(i, 4, "true") == 0) { i += 4; Value v; v.type = JType::Bool; v.b = true; return v; }
        if (s.compare(i, 5, "false") == 0) { i += 5; Value v; v.type = JType::Bool; v.b = false; return v; }
        fail("invalid literal");
        return Value{};
    }
    void parseLiteral(const char* lit) {
        std::size_t n = std::strlen(lit);
        if (s.compare(i, n, lit) != 0) fail("invalid literal");
        i += n;
    }
    Value parseArray() {
        DepthGuard guard(*this);
        ++i; // '['
        Value v; v.type = JType::Array;
        ws();
        if (i < s.size() && s[i] == ']') { ++i; return v; }
        while (true) {
            v.arr.push_back(parseValue());
            ws();
            if (i >= s.size()) fail("unterminated array");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; break; }
            fail("expected ',' or ']'");
        }
        return v;
    }
    Value parseObject() {
        DepthGuard guard(*this);
        ++i; // '{'
        Value v; v.type = JType::Object;
        ws();
        if (i < s.size() && s[i] == '}') { ++i; return v; }
        while (true) {
            ws();
            if (i >= s.size() || s[i] != '"') fail("expected key string");
            std::string key = parseString();
            ws();
            if (i >= s.size() || s[i] != ':') fail("expected ':'");
            ++i;
            Value val = parseValue();
            v.obj.emplace_back(std::move(key), std::move(val));
            ws();
            if (i >= s.size()) fail("unterminated object");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; break; }
            fail("expected ',' or '}'");
        }
        return v;
    }
};

// 序列化侧对称保护：解析已限制 128 层，这里再兜一道（程序内构造的深结构同样会爆栈）
constexpr int kMaxDumpDepth = 256;

void dumpImpl(const Value& v, int indent, int depth, std::string& out) {
    if (depth > kMaxDumpDepth) {
        out += "null"; // 超限截断为 null，而不是递归到栈溢出
        return;
    }
    auto nl = [&]() { if (indent > 0) { out += '\n'; out.append(static_cast<std::size_t>(indent * (depth + 1)), ' '); } };
    auto sp = [&]() { if (indent > 0) { out.append(static_cast<std::size_t>(indent * depth), ' '); } };
    switch (v.type) {
        case JType::Null: out += "null"; break;
        case JType::Bool: out += v.b ? "true" : "false"; break;
        case JType::Number: {
            char buf[64];
            if (!std::isfinite(v.num)) {
                // JSON 无 NaN/Inf 表示；按 JSON.stringify 惯例降级为 null
                out += "null";
                break;
            }
            if (std::floor(v.num) == v.num && std::fabs(v.num) < 1e15) {
                std::snprintf(buf, sizeof(buf), "%.0f", v.num);
            } else {
                // 优先输出可读的短表示：若 6 位有效数字能还原同一 float32 值就采用。
                // 配置字段多为 float，可避免 0.35 被写成 0.349999994 这类噪声。
                std::snprintf(buf, sizeof(buf), "%.6g", v.num);
                const double back = std::strtod(buf, nullptr);
                if (static_cast<float>(back) != static_cast<float>(v.num)) {
                    std::snprintf(buf, sizeof(buf), "%.10g", v.num);
                }
            }
            out += buf;
            break;
        }
        case JType::String: {
            out += '"';
            for (char c : v.str) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default: out += c; break;
                }
            }
            out += '"';
            break;
        }
        case JType::Array: {
            out += '[';
            for (std::size_t k = 0; k < v.arr.size(); ++k) {
                if (k) out += ',';
                nl(); sp();
                dumpImpl(v.arr[k], indent, depth + 1, out);
            }
            if (!v.arr.empty()) { nl(); sp(); }
            out += ']';
            break;
        }
        case JType::Object: {
            out += '{';
            for (std::size_t k = 0; k < v.obj.size(); ++k) {
                if (k) out += ',';
                nl(); sp();
                out += '"'; out += v.obj[k].first; out += "\":";
                if (indent > 0) out += ' ';
                dumpImpl(v.obj[k].second, indent, depth + 1, out);
            }
            if (!v.obj.empty()) { nl(); sp(); }
            out += '}';
            break;
        }
    }
}

} // namespace

bool parse(const std::string& text, Value& out, std::string& err) {
    err.clear();
    Parser p(text, err);
    try {
        p.ws();
        out = p.parseValue();
        p.ws();
        if (p.i < text.size()) { err = "trailing characters after JSON value"; return false; }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

Value parseOrThrow(const std::string& text) {
    Value v;
    std::string err;
    if (!parse(text, v, err)) throw std::runtime_error("json parse error: " + err);
    return v;
}

std::string dump(const Value& v, int indent) {
    std::string out;
    dumpImpl(v, indent, 0, out);
    return out;
}

} // namespace aim::json
