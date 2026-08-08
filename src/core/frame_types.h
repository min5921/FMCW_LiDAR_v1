#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fmcw {

inline constexpr std::uint32_t kRawFrameFormatVersion = 1;
inline constexpr std::uint32_t kLegacyRawFrameBatchFormatVersion = 2;
inline constexpr std::uint32_t kRawFrameBatchFormatVersion = 3;

enum class FrameKind {
  FullChirpPeriod,
};

enum class SampleFormat {
  SignedInt16,
  UnsignedOffsetBinary12LeftAligned,
};

inline std::int16_t sampleAsSignedInt16(std::int16_t stored_sample, SampleFormat format) {
  if (format == SampleFormat::UnsignedOffsetBinary12LeftAligned) {
    const auto raw = static_cast<std::uint16_t>(stored_sample);
    const auto centered = static_cast<std::int32_t>(raw & 0xFFF0U) - 32768;
    return static_cast<std::int16_t>(centered);
  }
  return stored_sample;
}

inline float sampleAsNormalizedFloat(std::int16_t stored_sample, SampleFormat format) {
  return static_cast<float>(sampleAsSignedInt16(stored_sample, format)) / 32768.0F;
}

enum class ByteOrder {
  LittleEndian,
  BigEndian,
};

enum class DigitizerChannel {
  A,
  B,
};

enum class ScanAxis : std::uint8_t {
  Unknown,
  X,
  Y,
};

enum class ScanDirection : std::uint8_t {
  Unknown,
  Increasing,
  Decreasing,
};

enum class ScanCoordinateSource : std::uint8_t {
  GeneratedRaster,
  McuTrajectory,
  Replay,
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

constexpr SegmentRange segmentRangeFromStartAndLength(std::uint32_t start_sample,
                                                       std::uint32_t length_samples) {
  if (length_samples > std::numeric_limits<std::uint32_t>::max() - start_sample) {
    return {start_sample, start_sample};
  }
  return {start_sample, start_sample + length_samples};
}

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
  std::uint32_t trajectory_sample_index = 0;
  float x_command = 0.0F;
  float y_command = 0.0F;
  // Stored field names are retained for format compatibility: X angle is
  // azimuth and Y angle is elevation, independent of source command columns.
  float x_angle_deg = 0.0F;
  float y_angle_deg = 0.0F;
  ScanAxis fast_axis = ScanAxis::Unknown;
  ScanDirection fast_axis_direction = ScanDirection::Unknown;
  ScanCoordinateSource source = ScanCoordinateSource::GeneratedRaster;
  bool angle_calibrated = false;
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

class SampleBuffer {
 public:
  using value_type = std::int16_t;
  using iterator = value_type*;
  using const_iterator = const value_type*;

  SampleBuffer() = default;
  SampleBuffer(std::initializer_list<value_type> values) : owned_(values) {}
  SampleBuffer(const SampleBuffer& other) { assign(other.begin(), other.end()); }
  SampleBuffer(SampleBuffer&& other) { moveFrom(std::move(other)); }

  SampleBuffer& operator=(const SampleBuffer& other) {
    if (this != &other) {
      assign(other.begin(), other.end());
    }
    return *this;
  }

  SampleBuffer& operator=(SampleBuffer&& other) {
    if (this != &other) {
      moveFrom(std::move(other));
    }
    return *this;
  }

  SampleBuffer& operator=(std::initializer_list<value_type> values) {
    assign(values.begin(), values.end());
    return *this;
  }

  SampleBuffer& operator=(const std::vector<value_type>& values) {
    assign(values.begin(), values.end());
    return *this;
  }

  void setView(value_type* data, std::size_t size) {
    std::vector<value_type>().swap(owned_);
    view_data_ = data;
    view_size_ = size;
  }

  bool isView() const { return view_data_ != nullptr; }
  std::size_t size() const { return isView() ? view_size_ : owned_.size(); }
  std::size_t capacity() const { return isView() ? view_size_ : owned_.capacity(); }
  bool empty() const { return size() == 0U; }
  value_type* data() { return isView() ? view_data_ : owned_.data(); }
  const value_type* data() const { return isView() ? view_data_ : owned_.data(); }
  iterator begin() { return data(); }
  iterator end() { return data() == nullptr ? nullptr : data() + size(); }
  const_iterator begin() const { return data(); }
  const_iterator end() const { return data() == nullptr ? nullptr : data() + size(); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }
  value_type& operator[](std::size_t index) { return data()[index]; }
  const value_type& operator[](std::size_t index) const { return data()[index]; }

  void resize(std::size_t size) {
    ensureOwned();
    owned_.resize(size);
  }

  void clear() {
    owned_.clear();
    view_data_ = nullptr;
    view_size_ = 0U;
  }

  void assign(std::size_t count, value_type value) {
    view_data_ = nullptr;
    view_size_ = 0U;
    owned_.assign(count, value);
  }

  template <typename Iterator>
  void assign(Iterator first, Iterator last) {
    view_data_ = nullptr;
    view_size_ = 0U;
    owned_.assign(first, last);
  }

  bool operator==(const SampleBuffer& other) const {
    return size() == other.size() && std::equal(begin(), end(), other.begin());
  }

  bool operator==(const std::vector<value_type>& other) const {
    return size() == other.size() && std::equal(begin(), end(), other.begin());
  }

 private:
  void ensureOwned() {
    if (!isView()) {
      return;
    }
    std::vector<value_type> copy(begin(), end());
    owned_ = std::move(copy);
    view_data_ = nullptr;
    view_size_ = 0U;
  }

  void moveFrom(SampleBuffer&& other) {
    if (other.isView()) {
      assign(other.begin(), other.end());
      return;
    }
    owned_ = std::move(other.owned_);
    view_data_ = nullptr;
    view_size_ = 0U;
  }

  std::vector<value_type> owned_;
  value_type* view_data_ = nullptr;
  std::size_t view_size_ = 0U;
};

struct RawFrame {
  RawFrameMetadata metadata;
  SampleBuffer samples;
};

using RawFramePtr = std::shared_ptr<const RawFrame>;

struct DmaBufferMetadata {
  std::uint32_t format_version = kRawFrameBatchFormatVersion;
  std::uint64_t sequence = 0;
  std::uint64_t completion_timestamp_ns = 0;
  // Runtime-only host timing. Raw storage formats do not serialize these fields.
  std::uint64_t acquisition_wakeup_timestamp_ns = 0;
  std::uint64_t ownership_ready_timestamp_ns = 0;
  std::uint64_t session_ready_timestamp_ns = 0;
  std::uint32_t record_count = 0;
  std::uint32_t record_length = 0;
  std::uint64_t dropped_buffer_count = 0;
  std::uint64_t missed_trigger_count = 0;
};

struct RawFrameBatch {
  DmaBufferMetadata metadata;
  SampleBuffer contiguous_samples;
  std::vector<RawFrame> records;
  std::shared_ptr<void> sample_owner;

  bool hasContiguousSamples() const {
    return metadata.record_count > 0U && metadata.record_length > 0U &&
        contiguous_samples.size() == static_cast<std::size_t>(metadata.record_count) *
            metadata.record_length;
  }
  bool hasExternalSampleStorage() const {
    return contiguous_samples.isView() && sample_owner != nullptr;
  }
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
  // ROS/RViz right-handed axes: +X forward, +Y left, +Z up.
  float x = std::numeric_limits<float>::quiet_NaN();
  float y = std::numeric_limits<float>::quiet_NaN();
  float z = std::numeric_limits<float>::quiet_NaN();
  float intensity = std::numeric_limits<float>::quiet_NaN();
  float velocity = std::numeric_limits<float>::quiet_NaN();
  float scan_x_command = std::numeric_limits<float>::quiet_NaN();
  float scan_y_command = std::numeric_limits<float>::quiet_NaN();
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
