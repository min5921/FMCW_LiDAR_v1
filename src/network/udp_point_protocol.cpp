#include "network/udp_point_protocol.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace fmcw {
namespace {

template <typename T>
void appendUnsigned(std::vector<std::uint8_t>& bytes, T value) {
  static_assert(std::is_unsigned_v<T>);
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
  }
}

void appendFloat(std::vector<std::uint8_t>& bytes, float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  appendUnsigned(bytes, bits);
}

template <typename T>
bool readUnsigned(const std::vector<std::uint8_t>& bytes, std::size_t& offset, T& value) {
  static_assert(std::is_unsigned_v<T>);
  if (offset + sizeof(T) > bytes.size()) {
    return false;
  }
  value = 0;
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    value |= static_cast<T>(bytes[offset + index]) << (index * 8U);
  }
  offset += sizeof(T);
  return true;
}

bool readFloat(const std::vector<std::uint8_t>& bytes, std::size_t& offset, float& value) {
  std::uint32_t bits = 0;
  if (!readUnsigned(bytes, offset, bits)) {
    return false;
  }
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

}  // namespace

std::vector<std::vector<std::uint8_t>> UdpPointProtocol::encodeFrame(const UdpPointFrame& frame,
                                                                      std::uint32_t points_per_packet,
                                                                      std::uint16_t version,
                                                                      std::string& error) {
  if (version != kUdpPointPacketVersion || points_per_packet == 0U) {
    error = "UDP point packet version or point count is invalid";
    return {};
  }
  if (kUdpPointHeaderBytes + static_cast<std::size_t>(points_per_packet) * kUdpPointStrideBytes >
      kMaxUdpPayloadBytes) {
    error = "UDP point packet exceeds the maximum datagram payload";
    return {};
  }
  const auto segment_count_size = std::max<std::size_t>(
      1U, (frame.points.size() + points_per_packet - 1U) / points_per_packet);
  if (segment_count_size > std::numeric_limits<std::uint16_t>::max()) {
    error = "UDP frame requires too many packet segments";
    return {};
  }
  const auto segment_count = static_cast<std::uint16_t>(segment_count_size);
  std::vector<std::vector<std::uint8_t>> packets;
  packets.reserve(segment_count);
  for (std::uint16_t segment = 0; segment < segment_count; ++segment) {
    const auto begin = std::min<std::size_t>(static_cast<std::size_t>(segment) * points_per_packet,
                                             frame.points.size());
    const auto end = std::min<std::size_t>(begin + points_per_packet, frame.points.size());
    const auto count = static_cast<std::uint16_t>(end - begin);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kUdpPointHeaderBytes + static_cast<std::size_t>(count) * kUdpPointStrideBytes);
    bytes.insert(bytes.end(), {'F', 'M', 'C', 'W'});
    appendUnsigned(bytes, version);
    appendUnsigned(bytes, static_cast<std::uint16_t>(kUdpPointHeaderBytes));
    appendUnsigned(bytes, frame.raster_frame_id);
    appendUnsigned(bytes, frame.timestamp_ns);
    appendUnsigned(bytes, frame.config_revision);
    appendUnsigned(bytes, segment_count);
    appendUnsigned(bytes, segment);
    appendUnsigned(bytes, count);
    appendUnsigned(bytes, static_cast<std::uint16_t>(kUdpPointStrideBytes));
    for (auto index = begin; index < end; ++index) {
      const auto& point = frame.points[index];
      appendFloat(bytes, point.x);
      appendFloat(bytes, point.y);
      appendFloat(bytes, point.z);
      appendFloat(bytes, point.intensity);
      appendFloat(bytes, point.velocity);
    }
    packets.push_back(std::move(bytes));
  }
  error.clear();
  return packets;
}

bool UdpPointProtocol::decodePacket(const std::vector<std::uint8_t>& bytes, UdpPointPacket& packet,
                                    std::string& error) {
  if (bytes.size() < kUdpPointHeaderBytes || bytes[0] != 'F' || bytes[1] != 'M' ||
      bytes[2] != 'C' || bytes[3] != 'W') {
    error = "UDP point packet magic or size is invalid";
    return false;
  }
  std::size_t offset = 4;
  std::uint16_t header_bytes = 0;
  std::uint16_t point_count = 0;
  std::uint16_t point_stride = 0;
  packet = {};
  if (!readUnsigned(bytes, offset, packet.version) || !readUnsigned(bytes, offset, header_bytes) ||
      !readUnsigned(bytes, offset, packet.raster_frame_id) ||
      !readUnsigned(bytes, offset, packet.timestamp_ns) ||
      !readUnsigned(bytes, offset, packet.config_revision) ||
      !readUnsigned(bytes, offset, packet.segment_count) ||
      !readUnsigned(bytes, offset, packet.segment_index) || !readUnsigned(bytes, offset, point_count) ||
      !readUnsigned(bytes, offset, point_stride) || packet.version != kUdpPointPacketVersion ||
      header_bytes != kUdpPointHeaderBytes || point_stride != kUdpPointStrideBytes ||
      packet.segment_count == 0U || packet.segment_index >= packet.segment_count ||
      bytes.size() != kUdpPointHeaderBytes + static_cast<std::size_t>(point_count) * point_stride) {
    error = "UDP point packet header is invalid";
    return false;
  }
  packet.points.resize(point_count);
  for (auto& point : packet.points) {
    if (!readFloat(bytes, offset, point.x) || !readFloat(bytes, offset, point.y) ||
        !readFloat(bytes, offset, point.z) || !readFloat(bytes, offset, point.intensity) ||
        !readFloat(bytes, offset, point.velocity)) {
      error = "UDP point packet payload is truncated";
      return false;
    }
    point.valid = true;
  }
  error.clear();
  return true;
}

}  // namespace fmcw
