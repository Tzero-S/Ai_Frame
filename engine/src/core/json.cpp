#include "core/json.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace aim::json {

namespace {

struct Parser {
    const std::string& s;
    std::size_t i = 0;
    std::string& err;
    Parser(const std::string& str, std::string& e) : s(str), err(e) {}

    [[noreturn]] void fail(const std::string& m) {
        err = m;
        throw std::runtime_error(m);
    }
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
                        if (i + 4 > s.size()) fail("bad unicode escape");
                        unsigned code = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= (h - '0');
                            else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
                            else fail("bad hex digit");
                        }
                        if (code < 0x80) out += static_cast<char>(code);
                        else if (code < 0x800) {
                            out += static_cast<char>(0xC0 | (code >> 6));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (code >> 12));
                            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        }
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
    Value parseNumber() {
        std::size_t start = i;
        if (s[i] == '-') ++i;
        while (i < s.size() && (s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                                s[i] == '+' || s[i] == '-' || (s[i] >= '0' && s[i] <= '9'))) {
            ++i;
        }
        std::string num = s.substr(start, i - start);
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

void dumpImpl(const Value& v, int indent, int depth, std::string& out) {
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
