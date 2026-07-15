#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace fmcw {

inline constexpr std::uint32_t kRawFrameFormatVersion = 1;
inline constexpr std::uint32_t kRawFrameBatchFormatVersion = 1;

enum class FrameKind {
  FullChirpPeriod,
};

enum class SampleFormat {
  SignedInt16,
};

enum class ByteOrder {
  LittleEndian,
  BigEndian,
};

enum class DigitizerChannel {
  A,
  B,
};

struct SegmentRange {
  // Half-open sample interval: [start_sample, end_sample_exclusive).
  std::uint32_t start_sample = 0;
  std::uint32_t end_sample_exclusive = 0;

  constexpr std::uint32_t length() const {
    return end_sample_exclusive >= start_sample ? end_sample_exclusive - start_sample : 0;
  }

  constexpr bool validFor(std::uint32_t record_length) const {
    return start_sample < end_sample_exclusive && end_sample_exclusive <= record_length;
  }
};

struct TriggerMetadata {
  std::uint64_t sequence = 0;
  std::uint64_t timestamp_ns = 0;
  std::int64_t period_jitter_ns = 0;
  std::uint32_t missed_trigger_count = 0;
  bool valid = false;
};

struct ScanPosition {
  std::uint32_t x_index = 0;
  std::uint32_t y_index = 0;
  float x_angle_deg = 0.0F;
  float y_angle_deg = 0.0F;
  bool valid = false;
};

struct OpticalStateReference {
  std::uint64_t revision = 0;
  bool laser_enabled = false;
  bool edfa_used = false;
  bool edfa_output_enabled = false;
};

struct RawFrameMetadata {
  std::uint32_t format_version = kRawFrameFormatVersion;
  FrameKind frame_kind = FrameKind::FullChirpPeriod;
  std::uint64_t frame_id = 0;
  std::uint64_t dma_buffer_sequence = 0;
  std::uint32_t record_index_in_buffer = 0;
  std::uint32_t records_in_buffer = 1;
  std::uint64_t host_timestamp_ns = 0;
  std::uint64_t config_revision = 0;
  TriggerMetadata trigger;
  ScanPosition scan_position;
  OpticalStateReference optical_state;
  DigitizerChannel channel = DigitizerChannel::A;
  SampleFormat sample_format = SampleFormat::SignedInt16;
  ByteOrder byte_order = ByteOrder::LittleEndian;
  double sample_rate_hz = 0.0;
  std::uint32_t record_length = 0;
  std::uint32_t pre_trigger_samples = 0;
  std::uint32_t post_trigger_samples = 0;
  SegmentRange up_segment;
  SegmentRange down_segment;
};

struct RawFrame {
  RawFrameMetadata metadata;
  std::vector<std::int16_t> samples;
};

using RawFramePtr = std::shared_ptr<const RawFrame>;

struct DmaBufferMetadata {
  std::uint32_t format_version = kRawFrameBatchFormatVersion;
  std::uint64_t sequence = 0;
  std::uint64_t completion_timestamp_ns = 0;
  std::uint64_t ownership_ready_timestamp_ns = 0;
  std::uint32_t record_count = 0;
  std::uint32_t record_length = 0;
  std::uint64_t dropped_buffer_count = 0;
  std::uint64_t missed_trigger_count = 0;
};

struct RawFrameBatch {
  DmaBufferMetadata metadata;
  std::vector<RawFrame> records;
};

using MutableRawFrameBatchPtr = std::shared_ptr<RawFrameBatch>;
using RawFrameBatchPtr = std::shared_ptr<const RawFrameBatch>;

inline RawFramePtr rawFrameAt(const RawFrameBatchPtr& batch, std::size_t index) {
  if (!batch || index >= batch->records.size()) {
    return {};
  }
  return RawFramePtr(batch, &batch->records[index]);
}

struct PointXYZI {
  // Legacy-compatible axes: X lateral, Y forward range, Z vertical.
  float x = std::numeric_limits<float>::quiet_NaN();
  float y = std::numeric_limits<float>::quiet_NaN();
  float z = std::numeric_limits<float>::quiet_NaN();
  float intensity = std::numeric_limits<float>::quiet_NaN();
  float velocity = std::numeric_limits<float>::quiet_NaN();
  bool valid = false;
};

enum class PeakTrackState : std::uint8_t {
  // Processing emits Invalid or Detected; remaining values preserve the processed stream layout.
  Invalid,
  Detected,
  Tracked,
  Reacquired,
  HeldLast,
  Lost,
};

struct PeakMeasurement {
  std::int32_t discrete_bin = -1;
  float peak_bin = std::numeric_limits<float>::quiet_NaN();
  float magnitude_db = std::numeric_limits<float>::quiet_NaN();
  PeakTrackState state = PeakTrackState::Invalid;
  bool valid = false;
};

struct ProcessedFrame {
  std::uint64_t frame_id = 0;
  std::uint64_t source_timestamp_ns = 0;
  std::uint64_t config_revision = 0;
  std::uint64_t processing_config_revision = 0;
  ScanPosition scan_position;
  std::vector<float> up_fft_magnitude_db;
  std::vector<float> down_fft_magnitude_db;
  PeakMeasurement up_peak;
  PeakMeasurement down_peak;
  float distance_m = std::numeric_limits<float>::quiet_NaN();
  float velocity_mps = std::numeric_limits<float>::quiet_NaN();
  PointXYZI point;
  bool measurement_valid = false;
  bool stop_requested = false;
  double processing_latency_ms = 0.0;
  std::string processing_note;
};

using ProcessedFramePtr = std::shared_ptr<const ProcessedFrame>;

}  // namespace fmcw
