#include "core/config_schema.h"
#include "core/json.h"
#include <initializer_list>
#include <cctype>

namespace aim {

namespace {
bool OneOf(const std::string& s, std::initializer_list<const char*> list) {
    for (const char* k : list) if (s == k) return true;
    return false;
}
const json::Value* Obj(const json::Value& v, const char* key) {
    const json::Value* p = v.find(key);
    return (p && p->isObject()) ? p : nullptr;
}
} // namespace

bool ConfigSchema::IsIpv4(const std::string& s) {
    int part = 0, digits = 0, value = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '.') {
            if (digits == 0 || value > 255) return false;
            ++part;
            if (part > 3) return false;
            digits = 0; value = 0;
        } else if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            ++digits;
            if (digits > 3) return false;
        } else {
            return false;
        }
    }
    return part == 3 && digits > 0 && value <= 255;
}

bool ConfigSchema::IsHex8(const std::string& s) {
    if (s.size() != 8) return false;
    for (char c : s) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

Result<void> ConfigSchema::ValidateJson(const std::string& text) {
    json::Value v;
    std::string err;
    if (!json::parse(text, v, err))
        return Result<void>::Fail(2001, "配置 JSON 解析失败: " + err, "检查 JSON 语法");
    if (!v.isObject())
        return Result<void>::Fail(2002, "配置根必须是对象", "参考 §8.2 schema 结构");

    if (v["schema_version"].asInt(-1) != 4)
        return Result<void>::Fail(2003, "schema_version 必须为 4", "通过迁移器升级配置");

    // required 分组
    static const char* kRequired[] = {"capture", "inference", "aim", "actuator", "weapon"};
    for (const char* k : kRequired) {
        if (!Obj(v, k))
            return Result<void>::Fail(2004, std::string("缺少必需分组: ") + k,
                                      "补齐该分组或使用默认配置重新生成");
    }

    // capture
    {
        const json::Value* cap = Obj(v, "capture");
        const std::string mode = (*cap)["mode"].asString();
        if (!OneOf(mode, {"dxgi", "gdi", "scrcpy"}))
            return Result<void>::Fail(2005, "capture.mode 非法: " + mode, "取值 dxgi|gdi|scrcpy");
        const json::Value* roi = Obj(*cap, "roi");
        if (roi) {
            const int edge = (*roi)["edge"].asInt(640);
            if (!(edge == 320 || edge == 480 || edge == 640 || edge == 800 || edge == 960))
                return Result<void>::Fail(2006, "capture.roi.edge 非法", "取值 320/480/640/800/960");
            const std::string rm = (*roi)["mode"].asString("center_square");
            if (!OneOf(rm, {"center_square", "full"}))
                return Result<void>::Fail(2007, "capture.roi.mode 非法", "取值 center_square|full");
        }
    }

    // inference
    {
        const json::Value* inf = Obj(v, "inference");
        const std::string backend = (*inf)["backend"].asString();
        if (!OneOf(backend, {"dml", "cpu", "tensorrt"}))
            return Result<void>::Fail(2008, "inference.backend 非法: " + backend, "取值 dml|cpu|tensorrt");
        const int fps = (*inf)["target_fps"].asInt(120);
        if (!(fps == 0 || fps == 30 || fps == 60 || fps == 120 || fps == 144 || fps == 240))
            return Result<void>::Fail(2009, "inference.target_fps 非法", "取值 0/30/60/120/144/240");
        const double conf = (*inf)["conf_threshold"].asNumber(0.35);
        if (conf < 0.01 || conf > 0.99)
            return Result<void>::Fail(2010, "inference.conf_threshold 超出 [0.01,0.99]", "调整置信度阈值");
    }

    // actuator
    {
        const json::Value* act = Obj(v, "actuator");
        const std::string ch = (*act)["channel"].asString();
        if (!OneOf(ch, {"sendinput", "ghub", "kmbox", "serial"}))
            return Result<void>::Fail(2011, "actuator.channel 非法: " + ch,
                                      "取值 sendinput|ghub|kmbox|serial");
        const json::Value* km = Obj(*act, "kmbox");
        if (km) {
            const std::string ip = (*km)["ip"].asString();
            if (!ip.empty() && !IsIpv4(ip))
                return Result<void>::Fail(2012, "actuator.kmbox.ip 非合法 IPv4: " + ip, "填写如 192.168.2.188");
            const int port = (*km)["port"].asInt(8088);
            if (port < 1 || port > 65535)
                return Result<void>::Fail(2013, "actuator.kmbox.port 超出 1..65535", "填写 1-65535");
            const std::string uid = (*km)["uid"].asString();
            if (!uid.empty() && !IsHex8(uid))
                return Result<void>::Fail(2014, "actuator.kmbox.uid 须为 8 位十六进制", "填写如 529C5C53");
            const json::Value* bz = Obj(*km, "bezier");
            if (bz) {
                const std::string type = (*bz)["type"].asString("ease_in");
                if (!OneOf(type, {"ease_in", "ease_out", "smoothing"}))
                    return Result<void>::Fail(2015, "bezier.type 非法: " + type,
                                              "取值 ease_in|ease_out|smoothing");
                const double st = (*bz)["strength"].asNumber(0.5);
                if (st < 0.0 || st > 1.0)
                    return Result<void>::Fail(2016, "bezier.strength 超出 [0,1]", "填写 0-1");
            }
        }
    }

    return Result<void>::Ok();
}

} // namespace aim
