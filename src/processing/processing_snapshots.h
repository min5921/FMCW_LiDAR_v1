#pragma once

#include "core/frame_types.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace fmcw {

struct WaveformSnapshot {
  std::uint64_t frame_id = 0;
  std::uint64_t config_revision = 0;
  DigitizerChannel channel = DigitizerChannel::A;
  double sample_rate_hz = 0.0;
  SegmentRange up_segment;
  SegmentRange down_segment;
  std::vector<float> normalized_samples;
};

struct FftSnapshot {
  std::uint64_t frame_id = 0;
  std::uint64_t processing_config_revision = 0;
  std::vector<float> up_magnitude_db;
  std::vector<float> down_magnitude_db;
  PeakMeasurement up_peak;
  PeakMeasurement down_peak;
};

struct ScanLineSnapshot {
  std::uint64_t last_frame_id = 0;
  std::uint64_t processing_config_revision = 0;
  std::uint32_t y_index = 0;
  std::vector<float> up_peak_index;
  std::vector<float> down_peak_index;
  std::vector<float> up_peak_value_db;
  std::vector<float> down_peak_value_db;
  std::vector<float> distance_m;
  std::vector<float> velocity_mps;
  std::vector<float> z_m;
  std::vector<std::uint8_t> up_peak_state;
  std::vector<std::uint8_t> down_peak_state;
  std::vector<std::uint8_t> valid;
};

struct BScanSnapshot {
  std::uint64_t last_frame_id = 0;
  std::uint64_t processing_config_revision = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t completed_lines = 0;
  std::vector<float> z_m;
  std::vector<std::uint8_t> valid;
};

class ProcessingSnapshotStore {
 public:
  void configure(std::uint32_t x_pixel_count, std::uint32_t y_line_count);
  void publish(const RawFrame& raw, const ProcessedFrame& processed);

  std::shared_ptr<const WaveformSnapshot> latestWaveform() const;
  std::shared_ptr<const FftSnapshot> latestFft() const;
  std::shared_ptr<const ScanLineSnapshot> latestScanLine() const;
  std::shared_ptr<const BScanSnapshot> latestBScan() const;

 private:
  void resetLine(std::uint32_t y_index);

  mutable std::mutex mutex_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::uint32_t active_y_ = 0;
  bool has_active_line_ = false;
  std::size_t line_fill_count_ = 0;
  ScanLineSnapshot line_work_;
  std::vector<std::uint8_t> line_filled_;
  BScanSnapshot bscan_work_;
  std::shared_ptr<const WaveformSnapshot> waveform_;
  std::shared_ptr<const FftSnapshot> fft_;
  std::shared_ptr<const ScanLineSnapshot> scan_line_;
  std::shared_ptr<const BScanSnapshot> bscan_;
};

}  // namespace fmcw
