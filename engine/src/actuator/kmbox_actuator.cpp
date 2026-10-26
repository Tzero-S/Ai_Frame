#include "actuator/kmbox_actuator.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace aim::actuator {

namespace {
bool EnsureWsa() {
    static const bool ok = [] {
        WSADATA d{};
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
}
} // namespace

KmboxActuator::KmboxActuator() {
    std::mt19937 rng(static_cast<unsigned>(std::random_device{}()));
    for (auto& b : mac_) b = static_cast<uint8_t>(rng() & 0xFF);
    rand_ = static_cast<uint32_t>(rng());
}

KmboxActuator::~KmboxActuator() { close(); }

bool KmboxActuator::configure(const ActuatorConfig& cfg) {
    bezier_enabled_ = cfg.kmbox.bezier.enabled;
    bezier_.type = cfg.kmbox.bezier.type;
    bezier_.strength = cfg.kmbox.bezier.strength;
    return init(cfg.kmbox.ip, cfg.kmbox.port, cfg.kmbox.uid);
}

bool KmboxActuator::init(const std::string& ip, int port, const std::string& uid,
                         std::string* err) {
    close();
    if (!EnsureWsa()) {
        if (err) *err = "WSAStartup failed";
        return false;
    }

    in_addr a{};
    if (InetPtonA(AF_INET, ip.c_str(), &a) != 1) {
        if (err) *err = "invalid ipv4: " + ip;
        return false;
    }
    if (port <= 0 || port > 65535) {
        if (err) *err = "invalid port";
        return false;
    }
    if (!ParseUid(uid, uid_)) {
        if (err) *err = "invalid uid (8 hex chars)";
        return false;
    }

    addr_be_ = a.S_un.S_addr;
    port_be_ = htons(static_cast<u_short>(port));

    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        if (err) *err = "socket() failed";
        return false;
    }
    sock_ = static_cast<uintptr_t>(s);
    ready_ = true;
    return true;
}

void KmboxActuator::close() {
    if (sock_ != static_cast<uintptr_t>(~static_cast<uintptr_t>(0))) {
        closesocket(static_cast<SOCKET>(sock_));
        sock_ = static_cast<uintptr_t>(~static_cast<uintptr_t>(0));
    }
    ready_ = false;
}

bool KmboxActuator::send_packet(KmboxCmd cmd, const KmboxMove& mv) {
    if (!ready_) return false;

    const auto pkt = BuildKmboxPacket(mac_, rand_++, index_++, cmd, mv);

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.S_un.S_addr = addr_be_;
    to.sin_port = port_be_;

    const int n = sendto(static_cast<SOCKET>(sock_),
                         reinterpret_cast<const char*>(pkt.data()),
                         static_cast<int>(pkt.size()), 0,
                         reinterpret_cast<sockaddr*>(&to), sizeof(to));
    if (n != static_cast<int>(pkt.size())) return false;
    ++packets_;
    return true;
}

void KmboxActuator::move(const spi::MoveCmd& cmd) {
    // 贝塞尔路径（§5.6.3）：按曲线把总位移切成若干增量逐包下发，
    // 避免"直线突进"这种机械特征。分片数随位移长度自适应（2..16）。
    if (cmd.bezier && bezier_enabled_) {
        const float dist = std::sqrt(cmd.dx_px * cmd.dx_px + cmd.dy_px * cmd.dy_px);
        int ticks = static_cast<int>(dist / 8.f);
        ticks = std::clamp(ticks, 2, 16);

        const auto incs = BezierTicks(Vec2F{0.f, 0.f}, Vec2F{cmd.dx_px, cmd.dy_px},
                                      ticks, bezier_);
        // 用"累计位置取整再差分"避免逐片四舍五入造成总位移丢失
        float cx = 0.f, cy = 0.f;
        int px = 0, py = 0;
        for (const auto& d : incs) {
            cx += d.x;
            cy += d.y;
            const int nx = static_cast<int>(std::lround(cx));
            const int ny = static_cast<int>(std::lround(cy));
            KmboxMove mv;
            mv.dx = static_cast<int32_t>(nx - px);
            mv.dy = static_cast<int32_t>(ny - py);
            px = nx;
            py = ny;
            if (mv.dx == 0 && mv.dy == 0) continue;
            send_packet(KmboxCmd::Bezier, mv);
        }
        return;
    }

    KmboxMove mv;
    mv.dx = static_cast<int32_t>(std::lround(cmd.dx_px));
    mv.dy = static_cast<int32_t>(std::lround(cmd.dy_px));
    send_packet(KmboxCmd::Move, mv);
}

void KmboxActuator::click(const spi::ClickCmd& cmd) {
    KmboxMove mv;
    mv.buttons = cmd.down ? 1 : 0;
    send_packet(KmboxCmd::Move, mv);
}

bool KmboxActuator::send_connect() { return send_packet(KmboxCmd::Connect, KmboxMove{}); }

bool KmboxActuator::send_bezier(const KmboxMove& mv) {
    return send_packet(KmboxCmd::Bezier, mv);
}

} // namespace aim::actuator
