#include "ipc/protocol.h"
#include <cstring>

namespace aim::ipc {

bool ParseRequest(const std::string& text, Request& out, std::string* err) {
    json::Value v;
    std::string e;
    if (!json::parse(text, v, e)) { if (err) *err = "IPC 请求 JSON 非法: " + e; return false; }
    if (!v.isObject()) { if (err) *err = "IPC 请求必须是对象"; return false; }
    out.id = v["id"].asString();
    out.cmd = v["cmd"].asString();
    out.ts = static_cast<int64_t>(v["ts"].asNumber(0.0));
    out.payload = v["payload"];
    if (out.id.empty()) { if (err) *err = "IPC 请求缺少 id"; return false; }
    if (out.cmd.empty()) { if (err) *err = "IPC 请求缺少 cmd"; return false; }
    return true;
}

bool ParseResponse(const std::string& text, Response& out, std::string* err) {
    json::Value v;
    std::string e;
    if (!json::parse(text, v, e)) { if (err) *err = "IPC 响应 JSON 非法: " + e; return false; }
    if (!v.isObject()) { if (err) *err = "IPC 响应必须是对象"; return false; }
    out.id = v["id"].asString();
    out.code = v["code"].asInt(0);
    out.msg = v["msg"].asString("ok");
    out.payload = v["payload"];
    return true;
}

std::string Serialize(const Request& r) {
    json::Value v;
    v["id"] = r.id;
    v["cmd"] = r.cmd;
    v["ts"] = static_cast<double>(r.ts);
    v["payload"] = r.payload;
    return json::dump(v, 0);
}

std::string Serialize(const Response& r) {
    json::Value v;
    v["id"] = r.id;
    v["code"] = r.code;
    v["msg"] = r.msg;
    v["payload"] = r.payload;
    return json::dump(v, 0);
}

bool IsKnownCommand(const std::string& c) {
    static const char* kKnown[] = {
        cmd::kEngineHello, cmd::kConfigGet, cmd::kConfigApply,
        cmd::kModelImport, cmd::kModelList, cmd::kModelSelect, cmd::kModelPreview,
        cmd::kCaptureStart, cmd::kCaptureStop, cmd::kActuatorTest,
        cmd::kStatusSnapshot, cmd::kTelemetryStream, cmd::kLogTail,
        cmd::kWeaponPatterns, cmd::kFovEstimate,
    };
    for (const char* k : kKnown) if (c == k) return true;
    // auth.* 家族
    if (c.rfind("auth.", 0) == 0) return true;
    return false;
}

} // namespace aim::ipc
