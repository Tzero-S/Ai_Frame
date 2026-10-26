#include <winsock2.h>
#include <ws2tcpip.h>

#include "aim_test.h"
#include "actuator/ghub_actuator.h"
#include "actuator/kmbox_actuator.h"
#include "actuator/serial_actuator.h"

#include <cstring>
#include <string>
#include <vector>

using namespace aim;

namespace {

struct UdpReceiver {
    SOCKET s = INVALID_SOCKET;
    int port = 0;

    bool open() {
        WSADATA w{};
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return false;

        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;

        int len = sizeof(a);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&a), &len) != 0) return false;
        port = ntohs(a.sin_port);

        DWORD timeout = 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        return true;
    }

    int receive(uint8_t* buf, int cap) {
        sockaddr_in from{};
        int flen = sizeof(from);
        return recvfrom(s, reinterpret_cast<char*>(buf), cap, 0,
                        reinterpret_cast<sockaddr*>(&from), &flen);
    }

    void close() {
        if (s != INVALID_SOCKET) closesocket(s);
        s = INVALID_SOCKET;
    }
};

} // namespace

AIM_TEST(kmbox_udp_move_packet) {
    UdpReceiver rx;
    AIM_EXPECT_TRUE(rx.open());
    if (rx.s == INVALID_SOCKET) return;

    actuator::KmboxActuator km;
    std::string err;
    AIM_EXPECT_TRUE(km.init("127.0.0.1", rx.port, "529C5C53", &err));

    spi::MoveCmd mv;
    mv.dx_px = 10.f;
    mv.dy_px = -20.f;
    km.move(mv);

    uint8_t buf[128]{};
    const int n = rx.receive(buf, sizeof(buf));
    AIM_EXPECT_EQ(n, 72);
    if (n == 72) {
        // cmd = 0xAEDE7345 小端
        AIM_EXPECT_EQ(static_cast<int>(buf[0x0C]), 0x45);
        AIM_EXPECT_EQ(static_cast<int>(buf[0x0D]), 0x73);
        AIM_EXPECT_EQ(static_cast<int>(buf[0x0E]), 0xDE);
        AIM_EXPECT_EQ(static_cast<int>(buf[0x0F]), 0xAE);

        int32_t dx = 0, dy = 0;
        std::memcpy(&dx, buf + 0x14, 4);
        std::memcpy(&dy, buf + 0x18, 4);
        AIM_EXPECT_EQ(dx, 10);
        AIM_EXPECT_EQ(dy, -20);
    }
    AIM_EXPECT_EQ(static_cast<long long>(km.packets_sent()), 1LL);

    rx.close();
}

// 贝塞尔分片（§5.6.3）：bezier=true 时按曲线切成多包，且总位移守恒
AIM_TEST(kmbox_bezier_split_keeps_total) {
    UdpReceiver rx;
    AIM_EXPECT_TRUE(rx.open());
    if (rx.s == INVALID_SOCKET) return;

    ActuatorConfig cfg;
    cfg.kmbox.ip = "127.0.0.1";
    cfg.kmbox.port = rx.port;
    cfg.kmbox.uid = "529C5C53";
    cfg.kmbox.bezier.enabled = true;
    cfg.kmbox.bezier.type = BezierType::EaseIn;
    cfg.kmbox.bezier.strength = 0.6f;

    actuator::KmboxActuator km;
    AIM_EXPECT_TRUE(km.configure(cfg));

    spi::MoveCmd mv;
    mv.dx_px = 80.f;
    mv.dy_px = 0.f;
    mv.bezier = true;
    km.move(mv);

    const long long sent = static_cast<long long>(km.packets_sent());
    AIM_EXPECT_TRUE(sent >= 2); // 必须真的被切片

    int total_dx = 0, total_dy = 0;
    uint8_t buf[128]{};
    for (long long i = 0; i < sent; ++i) {
        const int n = rx.receive(buf, sizeof(buf));
        if (n != 72) break;
        int32_t dx = 0, dy = 0;
        std::memcpy(&dx, buf + 0x14, 4);
        std::memcpy(&dy, buf + 0x18, 4);
        total_dx += dx;
        total_dy += dy;
    }
    AIM_EXPECT_EQ(total_dx, 80); // 取整误差被"累计差分"抵消
    AIM_EXPECT_EQ(total_dy, 0);

    rx.close();
}

AIM_TEST(kmbox_udp_connect_packet) {    UdpReceiver rx;
    AIM_EXPECT_TRUE(rx.open());
    if (rx.s == INVALID_SOCKET) return;

    actuator::KmboxActuator km;
    std::string err;
    AIM_EXPECT_TRUE(km.init("127.0.0.1", rx.port, "529C5C53", &err));
    AIM_EXPECT_TRUE(km.send_connect());

    uint8_t buf[128]{};
    const int n = rx.receive(buf, sizeof(buf));
    AIM_EXPECT_EQ(n, 72);
    if (n == 72) {
        // connect = 0xAF3C2828 小端
        AIM_EXPECT_EQ(static_cast<int>(buf[0x0C]), 0x28);
        AIM_EXPECT_EQ(static_cast<int>(buf[0x0D]), 0x28);
        AIM_EXPECT_EQ(static_cast<int>(buf[0x0E]), 0x3C);
        AIM_EXPECT_EQ(static_cast<int>(buf[0x0F]), 0xAF);
    }

    rx.close();
}

AIM_TEST(kmbox_invalid_init) {
    actuator::KmboxActuator km;
    std::string err;
    AIM_EXPECT_FALSE(km.init("999.1.1.1", 8088, "529C5C53", &err));
    AIM_EXPECT_FALSE(km.init("127.0.0.1", 8088, "XYZ", &err));
    AIM_EXPECT_FALSE(km.ready());
}

AIM_TEST(serial_enumerate_and_bogus_init) {
    const auto ports = actuator::EnumerateSerialPorts();
    for (const auto& p : ports) {
        AIM_EXPECT_TRUE(p.rfind("COM", 0) == 0);
    }

    actuator::SerialActuator se;
    std::string err;
    AIM_EXPECT_FALSE(se.init("COM_DOES_NOT_EXIST_999", 115200, &err));
    AIM_EXPECT_FALSE(se.is_open());
}

AIM_TEST(ghub_bogus_device_fails) {
    actuator::GhubActuator g;
    std::string err;
    AIM_EXPECT_FALSE(g.init("\\\\.\\AIM_NO_SUCH_GHUB_DEVICE", &err));
    AIM_EXPECT_FALSE(g.is_open());
}

int main() {
    return aim::test::run_all();
}
