#include "opsec/network_policy.h"

namespace aim::opsec {

NetworkDecision EvaluatePeer(const NetworkPolicy& policy,
                             const TlsPeerInfo& peer,
                             bool request_signed) {
    if (peer.tls_version < policy.min_tls_version) {
        return NetworkDecision::RejectTlsVersion;
    }
    if (!policy.pinned_sha256.empty()) {
        bool found = false;
        for (const auto& pin : policy.pinned_sha256) {
            if (pin == peer.cert_sha256) {
                found = true;
                break;
            }
        }
        if (!found) return NetworkDecision::RejectPin;
    }
    if (policy.require_signed_requests && !request_signed) {
        return NetworkDecision::RejectSignature;
    }
    return NetworkDecision::Allow;
}

const char* to_string(NetworkDecision d) {
    switch (d) {
        case NetworkDecision::Allow:             return "allow";
        case NetworkDecision::RejectTlsVersion:  return "reject_tls_version";
        case NetworkDecision::RejectPin:         return "reject_pin";
        case NetworkDecision::RejectSignature:   return "reject_signature";
    }
    return "reject";
}

} // namespace aim::opsec
