#include "processing/processing_snapshots.h"

#include <algorithm>
#include <limits>

namespace fmcw {

void ProcessingSnapshotStore::configure(std::uint32_t x_pixel_count, std::uint32_t y_line_count) {
  std::lock_guard<std::mutex> lock(mutex_);
  width_ = x_pixel_count;
  height_ = y_line_count;
  has_active_line_ = false;
  line_fill_count_ = 0;
  line_work_ = {};
  line_filled_.assign(width_, 0U);
  bscan_work_ = {};
  bscan_work_.width = width_;
  bscan_work_.height = height_;
  bscan_work_.z_m.assign(static_cast<std::size_t>(width_) * height_, 0.0F);
  bscan_work_.valid.assign(static_cast<std::size_t>(width_) * height_, 0U);
  waveform_.reset();
  fft_.reset();
  scan_line_.reset();
  bscan_.reset();
}

void ProcessingSnapshotStore::resetLine(std::uint32_t y_index) {
  active_y_ = y_index;
  has_active_line_ = true;
  line_fill_count_ = 0;
  line_work_ = {};
  line_work_.y_index = y_index;
  const float invalid = std::numeric_limits<float>::quiet_NaN();
  line_work_.up_peak_index.assign(width_, invalid);
  line_work_.down_peak_index.assign(width_, invalid);
  line_work_.up_peak_value_db.assign(width_, invalid);
  line_work_.down_peak_value_db.assign(width_, invalid);
  line_work_.distance_m.assign(width_, invalid);
  line_work_.velocity_mps.assign(width_, invalid);
  line_work_.z_m.assign(width_, invalid);
  line_work_.up_peak_state.assign(width_, static_cast<std::uint8_t>(PeakTrackState::Invalid));
  line_work_.down_peak_state.assign(width_, static_cast<std::uint8_t>(PeakTrackState::Invalid));
  line_work_.valid.assign(width_, 0U);
  line_filled_.assign(width_, 0U);
}

void ProcessingSnapshotStore::publish(const RawFrame& raw, const ProcessedFrame& processed) {
  auto waveform = std::make_shared<WaveformSnapshot>();
  waveform->frame_id = raw.metadata.frame_id;
  waveform->config_revision = raw.metadata.config_revision;
  waveform->channel = raw.metadata.channel;
  waveform->sample_rate_hz = raw.metadata.sample_rate_hz;
  waveform->up_segment = raw.metadata.up_segment;
  waveform->down_segment = raw.metadata.down_segment;
  waveform->normalized_samples.resize(raw.samples.size());
  std::transform(raw.samples.begin(), raw.samples.end(), waveform->normalized_samples.begin(),
                 [](std::int16_t value) { return static_cast<float>(value) / 32768.0F; });

  auto fft = std::make_shared<FftSnapshot>();
  fft->frame_id = processed.frame_id;
  fft->processing_config_revision = processed.processing_config_revision;
  fft->up_magnitude_db = processed.up_fft_magnitude_db;
  fft->down_magnitude_db = processed.down_fft_magnitude_db;
  fft->up_peak = processed.up_peak;
  fft->down_peak = processed.down_peak;

  std::lock_guard<std::mutex> lock(mutex_);
  waveform_ = std::move(waveform);
  fft_ = std::move(fft);
  if (!processed.scan_position.valid || processed.scan_position.x_index >= width_ ||
      processed.scan_position.y_index >= height_ || width_ == 0U || height_ == 0U) {
    return;
  }
  if (!has_active_line_ || active_y_ != processed.scan_position.y_index) {
    resetLine(processed.scan_position.y_index);
  }
  const auto x = processed.scan_position.x_index;
  if (line_filled_[x] == 0U) {
    line_filled_[x] = 1U;
    ++line_fill_count_;
  }
  line_work_.last_frame_id = processed.frame_id;
  line_work_.processing_config_revision = processed.processing_config_revision;
  line_work_.up_peak_index[x] = processed.up_peak.interpolated_bin;
  line_work_.down_peak_index[x] = processed.down_peak.interpolated_bin;
  line_work_.up_peak_value_db[x] = processed.up_peak.magnitude_db;
  line_work_.down_peak_value_db[x] = processed.down_peak.magnitude_db;
  line_work_.distance_m[x] = processed.distance_m;
  line_work_.velocity_mps[x] = processed.velocity_mps;
  line_work_.z_m[x] = processed.point.z;
  line_work_.up_peak_state[x] = static_cast<std::uint8_t>(processed.up_peak.state);
  line_work_.down_peak_state[x] = static_cast<std::uint8_t>(processed.down_peak.state);
  line_work_.valid[x] = processed.measurement_valid ? 1U : 0U;

  if (line_fill_count_ != width_) {
    return;
  }
  scan_line_ = std::make_shared<ScanLineSnapshot>(line_work_);
  const auto row_offset = static_cast<std::size_t>(active_y_) * width_;
  for (std::uint32_t index = 0; index < width_; ++index) {
    bscan_work_.z_m[row_offset + index] = line_work_.valid[index] != 0U
        ? line_work_.z_m[index]
        : 0.0F;
    bscan_work_.valid[row_offset + index] = line_work_.valid[index];
  }
  bscan_work_.last_frame_id = processed.frame_id;
  bscan_work_.processing_config_revision = processed.processing_config_revision;
  bscan_work_.completed_lines = std::min(height_, bscan_work_.completed_lines + 1U);
  bscan_ = std::make_shared<BScanSnapshot>(bscan_work_);
  has_active_line_ = false;
}

std::shared_ptr<const WaveformSnapshot> ProcessingSnapshotStore::latestWaveform() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return waveform_;
}

std::shared_ptr<const FftSnapshot> ProcessingSnapshotStore::latestFft() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return fft_;
}

std::shared_ptr<const ScanLineSnapshot> ProcessingSnapshotStore::latestScanLine() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return scan_line_;
}

std::shared_ptr<const BScanSnapshot> ProcessingSnapshotStore::latestBScan() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return bscan_;
}

}  // namespace fmcw
