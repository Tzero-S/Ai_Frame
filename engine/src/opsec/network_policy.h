#pragma once
#include <string>
#include <vector>

// L3 网络层策略（§5.12.3）：TLS 1.3 下限、证书固定、请求签名。
// 实际 TLS 客户端由授权模块（M10）实现；此处提供可单测的策略判定。
namespace aim::opsec {

struct NetworkPolicy {
    int min_tls_version = 13;              // 13 = TLS 1.3
    std::vector<std::string> pinned_sha256; // 证书 SHA-256 白名单
    bool require_signed_requests = true;   // 请求必须带签名 + 时间戳 + nonce
};

struct TlsPeerInfo {
    int tls_version = 0;
    std::string cert_sha256;
};

enum class NetworkDecision {
    Allow,
    RejectTlsVersion,
    RejectPin,
    RejectSignature,
};

NetworkDecision EvaluatePeer(const NetworkPolicy& policy,
                             const TlsPeerInfo& peer,
                             bool request_signed);

const char* to_string(NetworkDecision d);

} // namespace aim::opsec
