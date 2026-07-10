#include "udp_sender.h"

#include <vector>
#include <cstring>
#include <cstdio>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

bool UdpSenderInit(UdpSender& s, const std::string& ip, int port)
{
    s.ready = false;
    s.sock = -1;

    s.sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s.sock < 0) {
        perror("socket");
        return false;
    }

    std::memset(&s.addr, 0, sizeof(s.addr));
    s.addr.sin_family = AF_INET;
    s.addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, ip.c_str(), &s.addr.sin_addr) != 1) {
        std::fprintf(stderr, "inet_pton failed for ip: %s\n", ip.c_str());
        close(s.sock);
        s.sock = -1;
        return false;
    }

    int sndbuf = 1 << 20;
    if (setsockopt(s.sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
        perror("setsockopt(SO_SNDBUF)");
        // 치명적이진 않으니 계속 진행 가능
    }

    s.ready = true;
    return true;
}

void UdpSenderClose(UdpSender& s)
{
    if (s.sock >= 0) {
        close(s.sock);
        s.sock = -1;
    }
    s.ready = false;
}

bool UdpSendFrameXYZIV(
    UdpSender& s,
    uint32_t frame_num,
    uint64_t timestamp_ns,
    const PointXYZIV* points,
    uint32_t total_points,
    uint32_t points_per_packet)
{
    if (!s.ready || s.sock < 0 || !points || total_points == 0 || points_per_packet == 0)
        return false;

    const uint32_t total_segments =
        (total_points + points_per_packet - 1) / points_per_packet;

    for (uint32_t seg = 0; seg < total_segments; ++seg) {
        const uint32_t start = seg * points_per_packet;
        const uint32_t remain = total_points - start;
        const uint32_t count = (remain > points_per_packet) ? points_per_packet : remain;

        UdpPacketHeader hdr{};
        hdr.magic = 0x50434432; // "PCD2"
        hdr.frame_num = frame_num;
        hdr.total_segments = total_segments;
        hdr.segment_index = seg;
        hdr.point_count = count;
        hdr.timestamp_ns = timestamp_ns;

        std::vector<uint8_t> packet(sizeof(UdpPacketHeader) + count * sizeof(PointXYZIV));

        std::memcpy(packet.data(), &hdr, sizeof(hdr));
        std::memcpy(packet.data() + sizeof(hdr),
            points + start,
            count * sizeof(PointXYZIV));

        const ssize_t sent = sendto(
            s.sock,
            packet.data(),
            packet.size(),
            0,
            reinterpret_cast<const sockaddr*>(&s.addr),
            sizeof(s.addr));

        if (sent < 0) {
            perror("sendto");
            return false;
        }

        if (static_cast<size_t>(sent) != packet.size()) {
            std::fprintf(stderr,
                "sendto partial send: sent=%zd, expected=%zu\n",
                sent, packet.size());
            return false;
        }
    }

    return true;
}