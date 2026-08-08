#include "fmcw_lidar_rviz/udp_point_protocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>


namespace fmcw_lidar_rviz {
namespace {

constexpr std::uint8_t kMagic[] = {'F', 'M', 'C', 'W'};

std::uint16_t readU16(const std::uint8_t* bytes) {
  return static_cast<std::uint16_t>(bytes[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint64_t readU64(const std::uint8_t* bytes) {
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

template <typename T>
void appendUnsigned(std::vector<std::uint8_t>& bytes, T value) {
  static_assert(std::is_unsigned<T>::value, "wire integer must be unsigned");
  for (std::size_t index = 0U; index < sizeof(T); ++index) {
    bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
  }
}

void appendFloat(std::vector<std::uint8_t>& bytes, float value) {
  std::uint32_t bits = 0U;
  static_assert(sizeof(bits) == sizeof(value), "float32 is required by the wire format");
  std::memcpy(&bits, &value, sizeof(bits));
  appendUnsigned(bytes, bits);
}

}  // namespace

bool decodePacket(const std::uint8_t* bytes, std::size_t size, Packet& packet,
                  std::string& error) {
  if (bytes == nullptr || size < kHeaderBytes ||
      !std::equal(std::begin(kMagic), std::end(kMagic), bytes)) {
    error = "datagram is shorter than the header or has invalid FMCW magic";
    return false;
  }

  const auto version = readU16(bytes + 4U);
  const auto header_bytes = readU16(bytes + 6U);
  const auto segment_count = readU16(bytes + 32U);
  const auto segment_index = readU16(bytes + 34U);
  const auto point_count = readU16(bytes + 36U);
  const auto point_stride = readU16(bytes + 38U);
  const auto expected_size = kHeaderBytes +
      static_cast<std::size_t>(point_count) * kPointStrideBytes;

  if (version != kProtocolVersion || header_bytes != kHeaderBytes ||
      point_stride != kPointStrideBytes || segment_count == 0U ||
      segment_index >= segment_count || point_count > kMaximumPointsPerPacket ||
      size != expected_size) {
    error = "FMCW UDP v2 header or payload size is invalid";
    return false;
  }

  packet = {};
  packet.raster_frame_id = readU64(bytes + 8U);
  packet.timestamp_ns = readU64(bytes + 16U);
  packet.config_revision = readU64(bytes + 24U);
  packet.segment_count = segment_count;
  packet.segment_index = segment_index;
  packet.point_count = point_count;
  packet.point_data.assign(bytes + kHeaderBytes, bytes + size);
  error.clear();
  return true;
}

bool encodeFrame(std::uint64_t raster_frame_id, std::uint64_t timestamp_ns,
                 std::uint64_t config_revision, const std::vector<PointXYZIV>& points,
                 std::size_t points_per_packet,
                 std::vector<std::vector<std::uint8_t>>& datagrams,
                 std::string& error) {
  if (points_per_packet == 0U || points_per_packet > kMaximumPointsPerPacket) {
    error = "points_per_packet is outside the FMCW UDP v2 range";
    return false;
  }
  const auto segment_count_size = std::max<std::size_t>(
      1U, (points.size() + points_per_packet - 1U) / points_per_packet);
  if (segment_count_size > std::numeric_limits<std::uint16_t>::max()) {
    error = "frame requires more than 65535 UDP segments";
    return false;
  }
  for (const auto& point : points) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(point.z) || !std::isfinite(point.intensity) ||
        !std::isfinite(point.velocity)) {
      error = "test sender point contains a non-finite value";
      return false;
    }
  }

  const auto segment_count = static_cast<std::uint16_t>(segment_count_size);
  datagrams.clear();
  datagrams.reserve(segment_count);
  for (std::uint16_t segment_index = 0U; segment_index < segment_count;
       ++segment_index) {
    const auto begin = std::min<std::size_t>(
        static_cast<std::size_t>(segment_index) * points_per_packet, points.size());
    const auto end = std::min<std::size_t>(begin + points_per_packet, points.size());
    const auto point_count = static_cast<std::uint16_t>(end - begin);
    std::vector<std::uint8_t> datagram;
    datagram.reserve(kHeaderBytes + static_cast<std::size_t>(point_count) *
                                        kPointStrideBytes);
    datagram.insert(datagram.end(), std::begin(kMagic), std::end(kMagic));
    appendUnsigned(datagram, kProtocolVersion);
    appendUnsigned(datagram, static_cast<std::uint16_t>(kHeaderBytes));
    appendUnsigned(datagram, raster_frame_id);
    appendUnsigned(datagram, timestamp_ns);
    appendUnsigned(datagram, config_revision);
    appendUnsigned(datagram, segment_count);
    appendUnsigned(datagram, segment_index);
    appendUnsigned(datagram, point_count);
    appendUnsigned(datagram, static_cast<std::uint16_t>(kPointStrideBytes));
    for (auto index = begin; index < end; ++index) {
      const auto& point = points[index];
      appendFloat(datagram, point.x);
      appendFloat(datagram, point.y);
      appendFloat(datagram, point.z);
      appendFloat(datagram, point.intensity);
      appendFloat(datagram, point.velocity);
    }
    datagrams.push_back(std::move(datagram));
  }
  error.clear();
  return true;
}

FrameAssembler::FrameAssembler(std::chrono::milliseconds timeout,
                               std::size_t max_inflight_frames,
                               std::size_t max_segments_per_frame,
                               std::size_t max_frame_points)
    : timeout_(timeout),
      max_inflight_frames_(max_inflight_frames),
      max_segments_per_frame_(max_segments_per_frame),
      max_frame_points_(max_frame_points) {
  if (timeout_.count() <= 0 || max_inflight_frames_ == 0U ||
      max_segments_per_frame_ == 0U || max_frame_points_ == 0U) {
    throw std::invalid_argument("frame assembler limits must be positive");
  }
}

bool FrameAssembler::add(Packet packet, CompletedFrame& completed) {
  return addAt(std::move(packet), Clock::now(), completed);
}

bool FrameAssembler::addAt(Packet packet, TimePoint now, CompletedFrame& completed) {
  completed = {};
  expireAt(now);
  if (packet.segment_count == 0U || packet.segment_index >= packet.segment_count ||
      packet.segment_count > max_segments_per_frame_ ||
      packet.point_data.size() != static_cast<std::size_t>(packet.point_count) *
                                      kPointStrideBytes) {
    ++statistics_.rejected_segment_layouts;
    return false;
  }

  auto existing = frames_.find(packet.raster_frame_id);
  if (existing != frames_.end() &&
      (existing->second.timestamp_ns != packet.timestamp_ns ||
       existing->second.config_revision != packet.config_revision ||
       existing->second.segment_count != packet.segment_count)) {
    frames_.erase(existing);
    ++statistics_.inconsistent_frames;
    existing = frames_.end();
  }

  if (existing == frames_.end()) {
    evictOldestIfFull();
    PartialFrame partial;
    partial.timestamp_ns = packet.timestamp_ns;
    partial.config_revision = packet.config_revision;
    partial.segment_count = packet.segment_count;
    partial.created_at = now;
    partial.updated_at = now;
    partial.segments.resize(packet.segment_count);
    partial.present.assign(packet.segment_count, 0U);
    existing = frames_.emplace(packet.raster_frame_id, std::move(partial)).first;
  }

  auto& partial = existing->second;
  if (partial.present[packet.segment_index] != 0U) {
    partial.updated_at = now;
    ++statistics_.duplicate_segments;
    return false;
  }
  if (partial.point_count + packet.point_count > max_frame_points_) {
    frames_.erase(existing);
    ++statistics_.oversized_frames;
    return false;
  }

  partial.point_count += packet.point_count;
  ++partial.received_segments;
  partial.updated_at = now;
  partial.present[packet.segment_index] = 1U;
  partial.segments[packet.segment_index] = std::move(packet.point_data);
  if (partial.received_segments != partial.segment_count) {
    return false;
  }

  completed.raster_frame_id = packet.raster_frame_id;
  completed.timestamp_ns = partial.timestamp_ns;
  completed.config_revision = partial.config_revision;
  completed.point_count = partial.point_count;
  completed.point_data.reserve(completed.point_count * kPointStrideBytes);
  for (auto& segment : partial.segments) {
    completed.point_data.insert(completed.point_data.end(), segment.begin(), segment.end());
  }
  frames_.erase(existing);
  ++statistics_.completed_frames;
  return true;
}

std::size_t FrameAssembler::expire() { return expireAt(Clock::now()); }

std::size_t FrameAssembler::expireAt(TimePoint now) {
  std::size_t count = 0U;
  for (auto iterator = frames_.begin(); iterator != frames_.end();) {
    if (now - iterator->second.updated_at >= timeout_) {
      iterator = frames_.erase(iterator);
      ++count;
    } else {
      ++iterator;
    }
  }
  statistics_.expired_frames += count;
  return count;
}

void FrameAssembler::evictOldestIfFull() {
  if (frames_.size() < max_inflight_frames_) {
    return;
  }
  const auto oldest = std::min_element(
      frames_.begin(), frames_.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.second.updated_at < rhs.second.updated_at;
      });
  if (oldest != frames_.end()) {
    frames_.erase(oldest);
    ++statistics_.evicted_frames;
  }
}

}  // namespace fmcw_lidar_rviz
