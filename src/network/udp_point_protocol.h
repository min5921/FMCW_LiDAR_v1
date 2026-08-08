#pragma once

#include "core/frame_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fmcw {

// Version 2 fixes XYZ semantics as ROS REP-103: +X forward, +Y left, +Z up.
// Version 1 used the same wire layout with legacy axis semantics and is rejected.
inline constexpr std::uint16_t kUdpPointPacketVersion = 2;
inline constexpr std::size_t kUdpPointHeaderBytes = 40;
inline constexpr std::size_t kUdpPointStrideBytes = 20;
inline constexpr std::size_t kMaxUdpPayloadBytes = 65507;

struct UdpPointFrame {
  // Point payload semantics are +X forward, +Y left, +Z up.
  std::uint64_t raster_frame_id = 0;
  std::uint64_t timestamp_ns = 0;
  std::uint64_t config_revision = 0;
  std::vector<PointXYZI> points;
};

struct UdpPointPacket {
  std::uint16_t version = 0;
  std::uint64_t raster_frame_id = 0;
  std::uint64_t timestamp_ns = 0;
  std::uint64_t config_revision = 0;
  std::uint16_t segment_count = 0;
  std::uint16_t segment_index = 0;
  std::vector<PointXYZI> points;
};

class UdpPointProtocol final {
 public:
  static std::vector<std::vector<std::uint8_t>> encodeFrame(const UdpPointFrame& frame,
                                                             std::uint32_t points_per_packet,
                                                             std::uint16_t version,
                                                             std::string& error);
  static bool decodePacket(const std::vector<std::uint8_t>& bytes, UdpPointPacket& packet,
                           std::string& error);
};

}  // namespace fmcw
