#pragma once

#include <cstdint>
#include <string>
#include <netinet/in.h>

#pragma pack(push, 1)

struct PointXYZIV
{
    float x;
    float y;
    float z;
    float intensity;
    float velocity;
};

struct UdpPacketHeader
{
    uint32_t magic;          // "PCD2" = 0x50434432
    uint32_t frame_num;
    uint32_t total_segments;
    uint32_t segment_index;
    uint32_t point_count;
    uint64_t timestamp_ns;
};

#pragma pack(pop)

struct UdpSender
{
    int sock = -1;
    sockaddr_in addr{};
    bool ready = false;
};

bool UdpSenderInit(UdpSender& s, const std::string& ip, int port);
void UdpSenderClose(UdpSender& s);

bool UdpSendFrameXYZIV(
    UdpSender& s,
    uint32_t frame_num,
    uint64_t timestamp_ns,
    const PointXYZIV* points,
    uint32_t total_points,
    uint32_t points_per_packet);