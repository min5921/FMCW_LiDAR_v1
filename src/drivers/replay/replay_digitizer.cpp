#include "drivers/replay/replay_digitizer.h"

#include "core/config_validation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

namespace fmcw {
namespace {

constexpr double kMaximumReplayBatchRateHz = 30.0;

std::uint64_t nowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

std::string ReplayDigitizer::name() const { return "Raw v1 replay digitizer"; }

DigitizerTelemetry ReplayDigitizer::telemetry() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return telemetry_;
}

bool ReplayDigitizer::configure(const SystemConfig& config, std::string& error) {
  const auto validation = ConfigValidator::validate(config);
  if (validation.hasErrors()) {
    error = "Replay digitizer rejected invalid configuration";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (telemetry_.device.running) {
    error = "Cannot configure replay while acquisition is running";
    return false;
  }
  config_ = config;
  configured_ = true;
  telemetry_.device.ready = false;
  telemetry_.device.detail = "Replay file configured";
  error.clear();
  return true;
}

bool ReplayDigitizer::openReader(std::string& error) {
  reader_.close();
  if (!reader_.open(std::filesystem::path(config_.runtime.replay_file), error)) {
    return false;
  }
  const auto& descriptor = reader_.streamDescriptor();
  if (descriptor.channel != config_.digitizer.channel ||
      descriptor.record_length != config_.digitizer.sample_point ||
      std::abs(descriptor.sample_rate_hz - config_.digitizer.sample_rate_hz) >
          std::max(1.0, config_.digitizer.sample_rate_hz * 1.0e-9)) {
    reader_.close();
    error = "Replay stream channel, sample rate, or record length does not match the active profile";
    return false;
  }
  return true;
}

bool ReplayDigitizer::connect(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_) {
    error = "Configure the replay source before connecting";
    return false;
  }
  if (!openReader(error)) {
    telemetry_.device.detail = error;
    return false;
  }
  telemetry_.device.connected = true;
  telemetry_.device.ready = true;
  telemetry_.device.detail = "Replay file ready: " + config_.runtime.replay_file;
  error.clear();
  return true;
}

void ReplayDigitizer::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  reader_.close();
  telemetry_.device = {};
  telemetry_.device.detail = "Replay disconnected";
  compatibility_batch_.reset();
  aborted_ = true;
  condition_.notify_all();
}

bool ReplayDigitizer::start(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_ || !telemetry_.device.connected || !telemetry_.device.ready) {
    error = "Replay source is not configured and connected";
    return false;
  }
  if (!openReader(error)) {
    telemetry_.device.detail = error;
    return false;
  }
  telemetry_.frames_received = 0;
  telemetry_.dma_buffers_received = 0;
  telemetry_.dma_buffer_drops = 0;
  telemetry_.trigger_misses = 0;
  telemetry_.dma_buffer_rate_hz = 0.0;
  telemetry_.dma_buffer_period_ms = 0.0;
  telemetry_.device.running = true;
  telemetry_.device.detail = "Replay acquisition active";
  compatibility_batch_.reset();
  compatibility_record_index_ = 0;
  next_batch_due_ = std::chrono::steady_clock::now();
  next_frame_id_ = 1;
  end_pending_ = false;
  aborted_ = false;
  error.clear();
  return true;
}

FrameWaitResult ReplayDigitizer::waitForBatch(MutableRawFrameBatchPtr& batch,
                                              std::chrono::milliseconds timeout,
                                              std::string& error) {
  std::unique_lock<std::mutex> lock(mutex_);
  batch.reset();
  if (!telemetry_.device.running || aborted_) {
    error.clear();
    return FrameWaitResult::Stopped;
  }
  if (end_pending_) {
    telemetry_.device.running = false;
    telemetry_.device.detail = "Replay complete";
    error.clear();
    return FrameWaitResult::Stopped;
  }
  if (timeout.count() <= 0) {
    error.clear();
    return FrameWaitResult::Timeout;
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < next_batch_due_) {
    const auto wake_at = std::min(next_batch_due_, deadline);
    condition_.wait_until(lock, wake_at, [this] {
      return aborted_ || !telemetry_.device.running;
    });
    if (aborted_ || !telemetry_.device.running) {
      error.clear();
      return FrameWaitResult::Stopped;
    }
    if (std::chrono::steady_clock::now() >= deadline &&
        std::chrono::steady_clock::now() < next_batch_due_) {
      error.clear();
      return FrameWaitResult::Timeout;
    }
  }

  auto mutable_batch = batch_pool_.acquire();
  mutable_batch->records.resize(config_.digitizer.records_per_buffer);
  std::size_t records_read = 0;
  bool reopened_empty_file = false;
  while (records_read < config_.digitizer.records_per_buffer) {
    const auto result = reader_.readNext(mutable_batch->records[records_read], error);
    if (result == ReplayReadResult::Error) {
      telemetry_.device.detail = error;
      return FrameWaitResult::Error;
    }
    if (result == ReplayReadResult::EndOfStream) {
      if (config_.runtime.replay_loop) {
        if (!openReader(error)) {
          telemetry_.device.detail = error;
          return FrameWaitResult::Error;
        }
        if (records_read == 0U && reopened_empty_file) {
          error = "Replay file contains no frame records";
          telemetry_.device.detail = error;
          return FrameWaitResult::Error;
        }
        reopened_empty_file = records_read == 0U;
        continue;
      }
      if (records_read == 0U) {
        telemetry_.device.running = false;
        telemetry_.device.detail = "Replay complete";
        error.clear();
        return FrameWaitResult::Stopped;
      }
      end_pending_ = true;
      break;
    }
    reopened_empty_file = false;
    auto& frame = mutable_batch->records[records_read];
    frame.metadata.frame_id = next_frame_id_;
    frame.metadata.trigger.sequence = next_frame_id_;
    ++next_frame_id_;
    ++records_read;
  }

  mutable_batch->records.resize(records_read);
  const auto sequence = telemetry_.dma_buffers_received;
  const auto completion_timestamp_ns = nowNs();
  const auto record_count = static_cast<std::uint32_t>(mutable_batch->records.size());
  for (std::uint32_t index = 0; index < record_count; ++index) {
    auto& metadata = mutable_batch->records[index].metadata;
    metadata.dma_buffer_sequence = sequence;
    metadata.record_index_in_buffer = index;
    metadata.records_in_buffer = record_count;
  }
  mutable_batch->metadata.sequence = sequence;
  mutable_batch->metadata.completion_timestamp_ns = completion_timestamp_ns;
  mutable_batch->metadata.ownership_ready_timestamp_ns = nowNs();
  mutable_batch->metadata.record_count = record_count;
  mutable_batch->metadata.record_length = config_.digitizer.sample_point;
  mutable_batch->metadata.dropped_buffer_count = telemetry_.dma_buffer_drops;
  mutable_batch->metadata.missed_trigger_count = telemetry_.trigger_misses;

  telemetry_.frames_received += record_count;
  ++telemetry_.dma_buffers_received;
  const auto physical_period = std::chrono::duration<double>(
      static_cast<double>(record_count) * derivedChirpPeriodSeconds(config_));
  const auto replay_period = std::chrono::duration<double>(1.0 / kMaximumReplayBatchRateHz);
  const auto paced_period = std::max(physical_period, replay_period);
  telemetry_.dma_buffer_period_ms =
      std::chrono::duration<double, std::milli>(paced_period).count();
  telemetry_.dma_buffer_rate_hz = 1000.0 / telemetry_.dma_buffer_period_ms;
  next_batch_due_ = std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(paced_period);
  batch = std::move(mutable_batch);
  error.clear();
  return FrameWaitResult::FrameReady;
}

FrameWaitResult ReplayDigitizer::waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout,
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

  MutableRawFrameBatchPtr new_batch;
  const auto result = waitForBatch(new_batch, timeout, error);
  if (result != FrameWaitResult::FrameReady || !new_batch || new_batch->records.empty()) {
    return result;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  compatibility_batch_ = std::move(new_batch);
  compatibility_record_index_ = 1;
  frame = compatibility_batch_->records.front();
  if (compatibility_record_index_ >= compatibility_batch_->records.size()) {
    compatibility_batch_.reset();
  }
  return FrameWaitResult::FrameReady;
}

bool ReplayDigitizer::abort(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  aborted_ = true;
  telemetry_.device.running = false;
  telemetry_.device.detail = "Replay aborted";
  compatibility_batch_.reset();
  condition_.notify_all();
  error.clear();
  return true;
}

bool ReplayDigitizer::stop(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  telemetry_.device.running = false;
  telemetry_.device.ready = telemetry_.device.connected;
  telemetry_.device.detail = telemetry_.device.connected ? "Replay ready" : "Replay disconnected";
  compatibility_batch_.reset();
  condition_.notify_all();
  error.clear();
  return true;
}

}  // namespace fmcw
