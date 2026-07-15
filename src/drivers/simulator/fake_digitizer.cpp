#include "drivers/simulator/fake_digitizer.h"

#include "core/config_validation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace fmcw {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kSignalTemplateCount = 32U;
constexpr double kDevelopmentSimulatorBatchRateHz = 30.0;

std::uint64_t nowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::uint64_t timestampNs(std::chrono::steady_clock::time_point value) {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      value.time_since_epoch()).count());
}

std::int16_t tone(std::uint32_t index, std::uint32_t length, double bin, double amplitude, double phase) {
  const double angle = 2.0 * kPi * bin * static_cast<double>(index) / static_cast<double>(length) + phase;
  const double sample = std::round(std::sin(angle) * amplitude);
  return static_cast<std::int16_t>(std::clamp(sample,
      static_cast<double>(std::numeric_limits<std::int16_t>::min()),
      static_cast<double>(std::numeric_limits<std::int16_t>::max())));
}

}  // namespace

FakeDigitizer::~FakeDigitizer() { disconnect(); }

std::string FakeDigitizer::name() const { return "Fake single-channel digitizer"; }

DigitizerTelemetry FakeDigitizer::telemetry() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return telemetry_;
}

bool FakeDigitizer::connect(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_) {
    error = "Configure the fake digitizer before connecting";
    return false;
  }
  telemetry_.device.connected = true;
  telemetry_.device.ready = true;
  telemetry_.device.detail = "Simulator ready";
  error.clear();
  return true;
}

void FakeDigitizer::disconnect() {
  stopProducer();
  std::lock_guard<std::mutex> lock(mutex_);
  telemetry_.device = {};
  telemetry_.device.detail = "Simulator disconnected";
  aborted_ = true;
  compatibility_batch_.reset();
  completed_dma_buffers_.clear();
  condition_.notify_all();
}

bool FakeDigitizer::configure(const SystemConfig& config, std::string& error) {
  const auto validation = ConfigValidator::validate(config);
  if (validation.hasErrors()) {
    error = "Fake digitizer rejected invalid configuration";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (telemetry_.device.running) {
    error = "Cannot configure fake digitizer while running";
    return false;
  }
  config_ = config;
  buildSignalTemplates();
  configured_ = true;
  telemetry_.device.ready = telemetry_.device.connected;
  telemetry_.device.detail = "Configured for channel " + toString(config_.digitizer.channel);
  error.clear();
  return true;
}

bool FakeDigitizer::start(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_ || !telemetry_.device.connected || !telemetry_.device.ready) {
    error = "Fake digitizer is not configured and connected";
    return false;
  }
  if (producer_thread_.joinable()) {
    error = "Fake digitizer producer from the previous run is still active";
    return false;
  }
  telemetry_.device.running = true;
  telemetry_.device.detail = config_.runtime.simulator_realtime_dma
      ? "ATS-rate simulator DMA ring active"
      : "Development simulator DMA ring active";
  telemetry_.frames_received = 0;
  telemetry_.dma_buffers_received = 0;
  telemetry_.dma_buffer_drops = 0;
  telemetry_.trigger_misses = 0;
  telemetry_.trigger_jitter_ns = 0.0;
  const double sweep_rate_hz = config_.laser.sweep_rate_hz;
  const double physical_period_seconds = static_cast<double>(config_.digitizer.records_per_buffer) /
      sweep_rate_hz;
  const double period_seconds = config_.runtime.simulator_realtime_dma
      ? physical_period_seconds
      : std::max(physical_period_seconds, 1.0 / kDevelopmentSimulatorBatchRateHz);
  batch_period_ = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(period_seconds));
  if (batch_period_ <= std::chrono::steady_clock::duration::zero()) {
    batch_period_ = std::chrono::nanoseconds(1);
  }
  telemetry_.dma_buffer_period_ms = period_seconds * 1000.0;
  telemetry_.dma_buffer_rate_hz = telemetry_.dma_buffer_period_ms > 0.0
      ? 1000.0 / telemetry_.dma_buffer_period_ms
      : 0.0;
  next_frame_id_ = 1;
  compatibility_batch_.reset();
  compatibility_record_index_ = 0;
  completed_dma_buffers_.clear();
  next_dma_sequence_ = 0;
  scheduled_frame_count_ = 0;
  dma_overflow_latched_ = false;
  source_exhausted_ = false;
  producer_stop_requested_ = false;
  next_batch_due_ = std::chrono::steady_clock::now() +
      (config_.runtime.simulator_realtime_dma ? batch_period_ : std::chrono::steady_clock::duration::zero());
  aborted_ = false;
  producer_thread_ = std::thread([this] { producerLoop(); });
  error.clear();
  return true;
}

FrameWaitResult FakeDigitizer::waitForBatch(MutableRawFrameBatchPtr& batch,
                                            std::chrono::milliseconds timeout,
                                            std::string& error) {
  std::unique_lock<std::mutex> lock(mutex_);
  batch.reset();
  if (!telemetry_.device.running || aborted_) {
    error.clear();
    return FrameWaitResult::Stopped;
  }
  if (timeout.count() <= 0) {
    error.clear();
    return FrameWaitResult::Timeout;
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  condition_.wait_until(lock, deadline, [this] {
    return aborted_ || !telemetry_.device.running || dma_overflow_latched_ ||
        !completed_dma_buffers_.empty() || source_exhausted_;
  });
  if (dma_overflow_latched_) {
    telemetry_.device.running = false;
    telemetry_.device.ready = telemetry_.device.connected;
    telemetry_.device.detail = "Simulator DMA buffer overflow; acquisition stopped";
    error = "Simulator DMA ring overflow: completed buffers exceeded dma_buffer_count before repost";
    return FrameWaitResult::Error;
  }
  if (aborted_ || !telemetry_.device.running) {
    error.clear();
    return FrameWaitResult::Stopped;
  }
  if (completed_dma_buffers_.empty()) {
    if (source_exhausted_) {
      telemetry_.device.running = false;
      telemetry_.device.detail = "Finite simulator acquisition complete";
      error.clear();
      return FrameWaitResult::Stopped;
    }
    error.clear();
    return FrameWaitResult::Timeout;
  }

  const auto completed = completed_dma_buffers_.front();
  completed_dma_buffers_.pop_front();
  condition_.notify_all();
  lock.unlock();

  const auto record_count = completed.record_count;
  auto mutable_batch = batch_pool_.acquire();
  mutable_batch->records.resize(record_count);
  for (std::uint32_t record_index = 0; record_index < record_count; ++record_index) {
    fillFrame(mutable_batch->records[record_index], next_frame_id_++, completed.sequence,
              record_index, record_count, completed.completion_timestamp_ns);
  }
  mutable_batch->metadata.sequence = completed.sequence;
  mutable_batch->metadata.completion_timestamp_ns = completed.completion_timestamp_ns;
  mutable_batch->metadata.ownership_ready_timestamp_ns = nowNs();
  mutable_batch->metadata.record_count = record_count;
  mutable_batch->metadata.record_length = config_.digitizer.sample_point;
  lock.lock();
  mutable_batch->metadata.dropped_buffer_count = telemetry_.dma_buffer_drops;
  mutable_batch->metadata.missed_trigger_count = telemetry_.trigger_misses;
  telemetry_.frames_received += record_count;
  ++telemetry_.dma_buffers_received;
  lock.unlock();
  batch = std::move(mutable_batch);
  error.clear();
  return FrameWaitResult::FrameReady;
}

FrameWaitResult FakeDigitizer::waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout,
                                            std::string& error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (compatibility_batch_ && compatibility_record_index_ < compatibility_batch_->records.size()) {
      frame = compatibility_batch_->records[compatibility_record_index_++];
      if (compatibility_record_index_ >= compatibility_batch_->records.size()) {
        compatibility_batch_.reset();
      }
      error.clear();
      return FrameWaitResult::FrameReady;
    }
  }

  MutableRawFrameBatchPtr batch;
  const auto result = waitForBatch(batch, timeout, error);
  if (result != FrameWaitResult::FrameReady || !batch || batch->records.empty()) {
    return result;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  compatibility_batch_ = std::move(batch);
  compatibility_record_index_ = 1;
  frame = compatibility_batch_->records.front();
  if (compatibility_record_index_ >= compatibility_batch_->records.size()) {
    compatibility_batch_.reset();
  }
  return FrameWaitResult::FrameReady;
}

bool FakeDigitizer::abort(std::string& error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    aborted_ = true;
    producer_stop_requested_ = true;
    telemetry_.device.running = false;
    telemetry_.device.detail = "Simulator acquisition aborted";
    compatibility_batch_.reset();
    completed_dma_buffers_.clear();
    condition_.notify_all();
  }
  if (producer_thread_.joinable()) {
    producer_thread_.join();
  }
  error.clear();
  return true;
}

bool FakeDigitizer::stop(std::string& error) {
  stopProducer();
  std::lock_guard<std::mutex> lock(mutex_);
  telemetry_.device.running = false;
  telemetry_.device.ready = telemetry_.device.connected;
  telemetry_.device.detail = telemetry_.device.connected ? "Simulator ready" : "Simulator disconnected";
  compatibility_batch_.reset();
  completed_dma_buffers_.clear();
  error.clear();
  return true;
}

void FakeDigitizer::producerLoop() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!producer_stop_requested_) {
    condition_.wait_until(lock, next_batch_due_, [this] {
      return producer_stop_requested_ || aborted_ || !telemetry_.device.running;
    });
    if (producer_stop_requested_ || aborted_ || !telemetry_.device.running) {
      break;
    }

    const auto now = std::chrono::steady_clock::now();
    while (!producer_stop_requested_ && next_batch_due_ <= now) {
      const bool finite = config_.digitizer.acquisition_mode != AcquisitionMode::Continuous;
      const auto finite_count = config_.digitizer.acquisition_mode == AcquisitionMode::Single
          ? 1ULL
          : static_cast<std::uint64_t>(config_.digitizer.finite_frame_count);
      if (finite && scheduled_frame_count_ >= finite_count) {
        source_exhausted_ = true;
        producer_stop_requested_ = true;
        condition_.notify_all();
        break;
      }

      const auto remaining = finite ? finite_count - scheduled_frame_count_ :
          static_cast<std::uint64_t>(config_.digitizer.records_per_buffer);
      const auto record_count = static_cast<std::uint32_t>(std::min<std::uint64_t>(
          config_.digitizer.records_per_buffer, remaining));
      if (completed_dma_buffers_.size() >= config_.digitizer.dma_buffer_count) {
        ++telemetry_.dma_buffer_drops;
        dma_overflow_latched_ = true;
        producer_stop_requested_ = true;
        telemetry_.device.detail = "Simulator DMA overflow latched";
        condition_.notify_all();
        break;
      }

      const auto lateness_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
          now - next_batch_due_).count();
      telemetry_.trigger_jitter_ns = static_cast<double>(std::max<std::int64_t>(lateness_ns, 0));
      completed_dma_buffers_.push_back({next_dma_sequence_++, timestampNs(next_batch_due_), record_count});
      scheduled_frame_count_ += record_count;

      next_batch_due_ += batch_period_;
      if (finite && scheduled_frame_count_ >= finite_count) {
        source_exhausted_ = true;
        producer_stop_requested_ = true;
      }
      condition_.notify_all();
    }
  }
}

void FakeDigitizer::stopProducer() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    producer_stop_requested_ = true;
    condition_.notify_all();
  }
  if (producer_thread_.joinable()) {
    producer_thread_.join();
  }
}

void FakeDigitizer::buildSignalTemplates() {
  signal_templates_.assign(kSignalTemplateCount,
                           std::vector<std::int16_t>(config_.digitizer.sample_point, 0));
  const auto& up = config_.chirp_segmentation.up_segment;
  const auto& down = config_.chirp_segmentation.down_segment;
  for (std::size_t template_index = 0; template_index < signal_templates_.size(); ++template_index) {
    const double phase = static_cast<double>(template_index) * 0.03;
    auto& samples = signal_templates_[template_index];
    for (std::uint32_t index = 0; index < up.length(); ++index) {
      samples[up.start_sample + index] = tone(index, up.length(), 37.0, 12000.0, phase);
    }
    for (std::uint32_t index = 0; index < down.length(); ++index) {
      samples[down.start_sample + index] = tone(index, down.length(), 43.0, 10000.0, -phase);
    }
  }
}

void FakeDigitizer::fillFrame(RawFrame& frame, std::uint64_t frame_id,
                              std::uint64_t batch_sequence, std::uint32_t record_index,
                              std::uint32_t records_in_batch,
                              std::uint64_t completion_timestamp_ns) const {
  frame.metadata = {};
  const auto template_index = static_cast<std::size_t>((frame_id - 1U) % signal_templates_.size());
  frame.samples = signal_templates_[template_index];
  const auto& up = config_.chirp_segmentation.up_segment;
  const auto& down = config_.chirp_segmentation.down_segment;

  auto& metadata = frame.metadata;
  metadata.frame_kind = FrameKind::FullChirpPeriod;
  metadata.frame_id = frame_id;
  metadata.dma_buffer_sequence = batch_sequence;
  metadata.record_index_in_buffer = record_index;
  metadata.records_in_buffer = records_in_batch;
  metadata.host_timestamp_ns = completion_timestamp_ns;
  metadata.trigger.sequence = frame_id;
  metadata.trigger.timestamp_ns = metadata.host_timestamp_ns;
  metadata.trigger.valid = true;
  metadata.channel = config_.digitizer.channel;
  metadata.sample_format = SampleFormat::SignedInt16;
  metadata.byte_order = ByteOrder::LittleEndian;
  metadata.sample_rate_hz = config_.digitizer.sample_rate_hz;
  metadata.record_length = config_.digitizer.sample_point;
  metadata.pre_trigger_samples = config_.digitizer.pre_trigger_samples;
  metadata.post_trigger_samples = config_.digitizer.post_trigger_samples;
  metadata.up_segment = up;
  metadata.down_segment = down;

  const auto zero_based = frame_id - 1;
  const auto y = static_cast<std::uint32_t>((zero_based / config_.scan.x_pixel_count) % config_.scan.y_line_count);
  auto x = static_cast<std::uint32_t>(zero_based % config_.scan.x_pixel_count);
  if (config_.scan.bidirectional && (y % 2U) != 0U) {
    x = config_.scan.x_pixel_count - 1U - x;
  }
  metadata.scan_position.x_index = x;
  metadata.scan_position.y_index = y;
  metadata.scan_position.x_angle_deg = static_cast<float>(config_.scan.x_start_deg +
      (config_.scan.x_end_deg - config_.scan.x_start_deg) * static_cast<double>(x) /
          static_cast<double>(config_.scan.x_pixel_count - 1U));
  metadata.scan_position.y_angle_deg = static_cast<float>(config_.scan.y_start_deg +
      (config_.scan.y_end_deg - config_.scan.y_start_deg) * static_cast<double>(y) /
          static_cast<double>(config_.scan.y_line_count - 1U));
  metadata.scan_position.valid = true;
}

}  // namespace fmcw
