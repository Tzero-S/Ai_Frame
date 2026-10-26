#pragma once
#include <cstdint>
#include <string>
#include "core/json.h"

// IPC 协议（§8.1）：控制面 ↔ 引擎，命名管道承载 JSON。
//   请求：{ "id": "req-1024", "cmd": "config.apply", "ts": 1761292800000, "payload": {...} }
//   响应：{ "id": "req-1024", "code": 0, "msg": "ok", "payload": {...} }
namespace aim::ipc {

namespace cmd {
inline constexpr const char* kEngineHello     = "engine.hello";
inline constexpr const char* kConfigGet       = "config.get";
inline constexpr const char* kConfigApply     = "config.apply";
inline constexpr const char* kModelImport     = "model.import";
inline constexpr const char* kModelList       = "model.list";
inline constexpr const char* kModelSelect     = "model.select";
inline constexpr const char* kModelPreview    = "model.preview";
inline constexpr const char* kCaptureStart    = "capture.start";
inline constexpr const char* kCaptureStop     = "capture.stop";
inline constexpr const char* kActuatorTest    = "actuator.test";
inline constexpr const char* kWeaponPatterns  = "weapon.patterns";
inline constexpr const char* kFovEstimate     = "fov.estimate";
inline constexpr const char* kStatusSnapshot  = "status.snapshot";
inline constexpr const char* kTelemetryStream = "telemetry.stream";
inline constexpr const char* kLogTail         = "log.tail";
inline constexpr const char* kAuthHandshake   = "auth.handshake";
inline constexpr const char* kAuthLogin       = "auth.login";
inline constexpr const char* kAuthHeartbeat   = "auth.heartbeat";
inline constexpr const char* kAuthStatus      = "auth.status";
} // namespace cmd

struct Request {
    std::string id;
    std::string cmd;
    int64_t ts = 0;
    json::Value payload;
};

struct Response {
    std::string id;
    int code = 0;
    std::string msg = "ok";
    json::Value payload;
};

bool ParseRequest(const std::string& text, Request& out, std::string* err = nullptr);
bool ParseResponse(const std::string& text, Response& out, std::string* err = nullptr);

std::string Serialize(const Request& r);
std::string Serialize(const Response& r);

// §8.1 命令表成员判定（含 auth.* 前缀）。
bool IsKnownCommand(const std::string& c);

} // namespace aim::ipc
