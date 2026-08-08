#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fmcw_lidar_rviz {

// Version 2 fixes XYZ semantics as ROS REP-103. Legacy v1 is rejected.
constexpr std::uint16_t kProtocolVersion = 2U;
constexpr std::size_t kHeaderBytes = 40U;
constexpr std::size_t kPointStrideBytes = 20U;
constexpr std::size_t kMaximumUdpPayloadBytes = 65507U;
constexpr std::size_t kMaximumPointsPerPacket =
    (kMaximumUdpPayloadBytes - kHeaderBytes) / kPointStrideBytes;

struct PointXYZIV {
  // ROS REP-103 sensor convention: +X forward, +Y left, +Z up.
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float intensity = 0.0F;
  float velocity = 0.0F;
};

struct Packet {
  std::uint64_t raster_frame_id = 0U;
  std::uint64_t timestamp_ns = 0U;
  std::uint64_t config_revision = 0U;
  std::uint16_t segment_count = 0U;
  std::uint16_t segment_index = 0U;
  std::uint16_t point_count = 0U;
  std::vector<std::uint8_t> point_data;
};

struct CompletedFrame {
  std::uint64_t raster_frame_id = 0U;
  std::uint64_t timestamp_ns = 0U;
  std::uint64_t config_revision = 0U;
  std::size_t point_count = 0U;
  std::vector<std::uint8_t> point_data;
};

bool decodePacket(const std::uint8_t* bytes, std::size_t size, Packet& packet,
                  std::string& error);

bool encodeFrame(std::uint64_t raster_frame_id, std::uint64_t timestamp_ns,
                 std::uint64_t config_revision, const std::vector<PointXYZIV>& points,
                 std::size_t points_per_packet,
                 std::vector<std::vector<std::uint8_t>>& datagrams,
                 std::string& error);

struct AssemblerStatistics {
  std::uint64_t completed_frames = 0U;
  std::uint64_t duplicate_segments = 0U;
  std::uint64_t expired_frames = 0U;
  std::uint64_t evicted_frames = 0U;
  std::uint64_t inconsistent_frames = 0U;
  std::uint64_t oversized_frames = 0U;
  std::uint64_t rejected_segment_layouts = 0U;
};

class FrameAssembler final {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  FrameAssembler(std::chrono::milliseconds timeout, std::size_t max_inflight_frames,
                 std::size_t max_segments_per_frame, std::size_t max_frame_points);

  bool add(Packet packet, CompletedFrame& completed);
  bool addAt(Packet packet, TimePoint now, CompletedFrame& completed);
  std::size_t expire();
  std::size_t expireAt(TimePoint now);

  std::size_t inflightCount() const { return frames_.size(); }
  const AssemblerStatistics& statistics() const { return statistics_; }

 private:
  struct PartialFrame {
    std::uint64_t timestamp_ns = 0U;
    std::uint64_t config_revision = 0U;
    std::uint16_t segment_count = 0U;
    TimePoint created_at{};
    TimePoint updated_at{};
    std::size_t point_count = 0U;
    std::size_t received_segments = 0U;
    std::vector<std::vector<std::uint8_t>> segments;
    std::vector<std::uint8_t> present;
  };

  void evictOldestIfFull();

  std::chrono::milliseconds timeout_;
  std::size_t max_inflight_frames_ = 0U;
  std::size_t max_segments_per_frame_ = 0U;
  std::size_t max_frame_points_ = 0U;
  std::unordered_map<std::uint64_t, PartialFrame> frames_;
  AssemblerStatistics statistics_;
};

}  // namespace fmcw_lidar_rviz
