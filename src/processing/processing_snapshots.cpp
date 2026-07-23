#include "processing/processing_snapshots.h"

#include <algorithm>
#include <limits>

namespace fmcw {

void ProcessingSnapshotStore::configure(std::uint32_t x_pixel_count, std::uint32_t y_line_count) {
  std::lock_guard<std::mutex> lock(mutex_);
  const float invalid = std::numeric_limits<float>::quiet_NaN();
  width_ = x_pixel_count;
  height_ = y_line_count;
  selected_record_index_ = width_ == 0U ? 0U : std::min(selected_record_index_, width_ - 1U);
  has_active_line_ = false;
  scan_frame_index_ = 0;
  line_fill_count_ = 0;
  line_work_ = {};
  line_filled_.assign(width_, 0U);
  bscan_work_ = {};
  bscan_work_.width = width_;
  bscan_work_.height = height_;
  bscan_work_.depth_m.assign(static_cast<std::size_t>(width_) * height_, invalid);
  bscan_work_.valid.assign(static_cast<std::size_t>(width_) * height_, 0U);
  point_cloud_work_ = {};
  point_cloud_work_.width = width_;
  point_cloud_work_.height = height_;
  point_cloud_work_.points.assign(static_cast<std::size_t>(width_) * height_, {});
  std::atomic_store_explicit(&waveform_, std::shared_ptr<const WaveformSnapshot>{},
                             std::memory_order_release);
  std::atomic_store_explicit(&fft_, std::shared_ptr<const FftSnapshot>{},
                             std::memory_order_release);
  std::atomic_store_explicit(&scan_line_, std::shared_ptr<const ScanLineSnapshot>{},
                             std::memory_order_release);
  std::atomic_store_explicit(&bscan_, std::shared_ptr<const BScanSnapshot>{},
                             std::memory_order_release);
  std::atomic_store_explicit(&point_cloud_, std::shared_ptr<const PointCloudSnapshot>{},
                             std::memory_order_release);
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
  line_work_.depth_m.assign(width_, invalid);
  line_work_.up_peak_state.assign(width_, static_cast<std::uint8_t>(PeakTrackState::Invalid));
  line_work_.down_peak_state.assign(width_, static_cast<std::uint8_t>(PeakTrackState::Invalid));
  line_work_.valid.assign(width_, 0U);
  line_filled_.assign(width_, 0U);
}

void ProcessingSnapshotStore::setSelectedRecordIndex(std::uint32_t record_index) {
  std::lock_guard<std::mutex> lock(mutex_);
  selected_record_index_ = width_ == 0U ? 0U : std::min(record_index, width_ - 1U);
  std::atomic_store_explicit(&waveform_, std::shared_ptr<const WaveformSnapshot>{},
                             std::memory_order_release);
  std::atomic_store_explicit(&fft_, std::shared_ptr<const FftSnapshot>{},
                             std::memory_order_release);
}

std::uint32_t ProcessingSnapshotStore::selectedRecordIndex() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return selected_record_index_;
}

void ProcessingSnapshotStore::publish(const RawFrame& raw, const ProcessedFrame& processed) {
  std::lock_guard<std::mutex> lock(mutex_);
  publishUnlocked(raw, processed);
}

bool ProcessingSnapshotStore::publishBatch(
    const RawFrameBatch& raw_batch,
    const std::vector<ProcessedFrame>& processed_batch) {
  if (raw_batch.records.size() != processed_batch.size()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (std::size_t index = 0; index < raw_batch.records.size(); ++index) {
    publishUnlocked(raw_batch.records[index], processed_batch[index]);
  }
  return true;
}

void ProcessingSnapshotStore::publishUnlocked(const RawFrame& raw,
                                               const ProcessedFrame& processed) {
  if (raw.metadata.record_index_in_buffer == selected_record_index_) {
    auto waveform = std::make_shared<WaveformSnapshot>();
    waveform->frame_id = raw.metadata.frame_id;
    waveform->dma_buffer_sequence = raw.metadata.dma_buffer_sequence;
    waveform->record_index_in_buffer = raw.metadata.record_index_in_buffer;
    waveform->records_in_buffer = raw.metadata.records_in_buffer;
    waveform->config_revision = raw.metadata.config_revision;
    waveform->channel = raw.metadata.channel;
    waveform->sample_rate_hz = raw.metadata.sample_rate_hz;
    waveform->up_segment = raw.metadata.up_segment;
    waveform->down_segment = raw.metadata.down_segment;
    waveform->full_scale_samples.resize(raw.samples.size());
    std::transform(raw.samples.begin(), raw.samples.end(), waveform->full_scale_samples.begin(),
                   [format = raw.metadata.sample_format](std::int16_t value) {
                     return sampleAsNormalizedFloat(value, format);
                   });

    auto fft = std::make_shared<FftSnapshot>();
    fft->frame_id = processed.frame_id;
    fft->dma_buffer_sequence = raw.metadata.dma_buffer_sequence;
    fft->record_index_in_buffer = raw.metadata.record_index_in_buffer;
    fft->records_in_buffer = raw.metadata.records_in_buffer;
    fft->processing_config_revision = processed.processing_config_revision;
    fft->up_magnitude_db = processed.up_fft_magnitude_db;
    fft->down_magnitude_db = processed.down_fft_magnitude_db;
    fft->up_peak = processed.up_peak;
    fft->down_peak = processed.down_peak;
    std::atomic_store_explicit(
        &waveform_, std::shared_ptr<const WaveformSnapshot>(std::move(waveform)),
        std::memory_order_release);
    std::atomic_store_explicit(
        &fft_, std::shared_ptr<const FftSnapshot>(std::move(fft)),
        std::memory_order_release);
  }
  if (!processed.scan_position.valid || processed.scan_position.x_index >= width_ ||
      processed.scan_position.y_index >= height_ || width_ == 0U || height_ == 0U) {
    return;
  }
  if (!has_active_line_ || active_y_ != processed.scan_position.y_index) {
    if (processed.scan_position.y_index == 0U) {
      if (bscan_work_.last_frame_id != 0U) {
        ++scan_frame_index_;
      }
      std::fill(bscan_work_.depth_m.begin(), bscan_work_.depth_m.end(),
                std::numeric_limits<float>::quiet_NaN());
      std::fill(bscan_work_.valid.begin(), bscan_work_.valid.end(), 0U);
      bscan_work_.completed_lines = 0U;
      bscan_work_.scan_frame_index = scan_frame_index_;
      std::fill(point_cloud_work_.points.begin(), point_cloud_work_.points.end(), PointXYZI{});
      point_cloud_work_.completed_lines = 0U;
      point_cloud_work_.complete = false;
      point_cloud_work_.scan_frame_index = scan_frame_index_;
    }
    resetLine(processed.scan_position.y_index);
  }
  const auto x = processed.scan_position.x_index;
  if (line_filled_[x] == 0U) {
    line_filled_[x] = 1U;
    ++line_fill_count_;
  }
  line_work_.last_frame_id = processed.frame_id;
  line_work_.processing_config_revision = processed.processing_config_revision;
  line_work_.up_peak_index[x] = processed.up_peak.peak_bin;
  line_work_.down_peak_index[x] = processed.down_peak.peak_bin;
  line_work_.up_peak_value_db[x] = processed.up_peak.magnitude_db;
  line_work_.down_peak_value_db[x] = processed.down_peak.magnitude_db;
  line_work_.distance_m[x] = processed.distance_m;
  line_work_.velocity_mps[x] = processed.velocity_mps;
  line_work_.depth_m[x] = processed.point.y;
  line_work_.up_peak_state[x] = static_cast<std::uint8_t>(processed.up_peak.state);
  line_work_.down_peak_state[x] = static_cast<std::uint8_t>(processed.down_peak.state);
  line_work_.valid[x] = processed.measurement_valid ? 1U : 0U;
  const auto point_offset = static_cast<std::size_t>(processed.scan_position.y_index) * width_ + x;
  point_cloud_work_.points[point_offset] = processed.point;

  if (line_fill_count_ != width_) {
    return;
  }
  std::atomic_store_explicit(
      &scan_line_, std::make_shared<const ScanLineSnapshot>(line_work_),
      std::memory_order_release);
  const auto row_offset = static_cast<std::size_t>(active_y_) * width_;
  for (std::uint32_t index = 0; index < width_; ++index) {
    bscan_work_.depth_m[row_offset + index] = line_work_.depth_m[index];
    bscan_work_.valid[row_offset + index] = line_work_.valid[index];
  }
  bscan_work_.last_frame_id = processed.frame_id;
  bscan_work_.processing_config_revision = processed.processing_config_revision;
  bscan_work_.completed_lines = std::min(height_, bscan_work_.completed_lines + 1U);
  std::atomic_store_explicit(
      &bscan_, std::make_shared<const BScanSnapshot>(bscan_work_),
      std::memory_order_release);
  point_cloud_work_.last_frame_id = processed.frame_id;
  point_cloud_work_.processing_config_revision = processed.processing_config_revision;
  point_cloud_work_.completed_lines = bscan_work_.completed_lines;
  point_cloud_work_.complete = point_cloud_work_.completed_lines == height_;
  if (point_cloud_work_.complete) {
    std::atomic_store_explicit(
        &point_cloud_, std::make_shared<const PointCloudSnapshot>(point_cloud_work_),
        std::memory_order_release);
  }
  has_active_line_ = false;
}

std::shared_ptr<const WaveformSnapshot> ProcessingSnapshotStore::latestWaveform() const {
  return std::atomic_load_explicit(&waveform_, std::memory_order_acquire);
}

std::shared_ptr<const FftSnapshot> ProcessingSnapshotStore::latestFft() const {
  return std::atomic_load_explicit(&fft_, std::memory_order_acquire);
}

std::shared_ptr<const ScanLineSnapshot> ProcessingSnapshotStore::latestScanLine() const {
  return std::atomic_load_explicit(&scan_line_, std::memory_order_acquire);
}

std::shared_ptr<const BScanSnapshot> ProcessingSnapshotStore::latestBScan() const {
  return std::atomic_load_explicit(&bscan_, std::memory_order_acquire);
}

std::shared_ptr<const PointCloudSnapshot> ProcessingSnapshotStore::latestPointCloud() const {
  return std::atomic_load_explicit(&point_cloud_, std::memory_order_acquire);
}

}  // namespace fmcw
