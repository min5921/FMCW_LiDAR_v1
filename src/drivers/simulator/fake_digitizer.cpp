#include "drivers/simulator/fake_digitizer.h"

#include "core/config_validation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace fmcw {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::uint64_t nowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::int16_t tone(std::uint32_t index, std::uint32_t length, double bin, double amplitude, double phase) {
  const double angle = 2.0 * kPi * bin * static_cast<double>(index) / static_cast<double>(length) + phase;
  const double sample = std::round(std::sin(angle) * amplitude);
  return static_cast<std::int16_t>(std::clamp(sample,
      static_cast<double>(std::numeric_limits<std::int16_t>::min()),
      static_cast<double>(std::numeric_limits<std::int16_t>::max())));
}

}  // namespace

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
  std::lock_guard<std::mutex> lock(mutex_);
  telemetry_.device = {};
  telemetry_.device.detail = "Simulator disconnected";
  aborted_ = true;
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
  telemetry_.device.running = true;
  telemetry_.device.detail = "Generating up-triggered full-period frames";
  telemetry_.frames_received = 0;
  telemetry_.dma_buffers_received = 0;
  telemetry_.dma_buffer_drops = 0;
  telemetry_.trigger_misses = 0;
  telemetry_.trigger_jitter_ns = 0.0;
  telemetry_.dma_buffer_period_ms = static_cast<double>(config_.digitizer.records_per_buffer) *
      config_.laser.chirp_period_us / 1000.0;
  telemetry_.dma_buffer_rate_hz = telemetry_.dma_buffer_period_ms > 0.0
      ? 1000.0 / telemetry_.dma_buffer_period_ms
      : 0.0;
  next_frame_id_ = 1;
  aborted_ = false;
  error.clear();
  return true;
}

FrameWaitResult FakeDigitizer::waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!telemetry_.device.running || aborted_) {
    error.clear();
    return FrameWaitResult::Stopped;
  }
  if (timeout.count() <= 0) {
    error.clear();
    return FrameWaitResult::Timeout;
  }
  if (config_.digitizer.acquisition_mode == AcquisitionMode::Finite &&
      telemetry_.frames_received >= config_.digitizer.finite_frame_count) {
    telemetry_.device.running = false;
    telemetry_.device.detail = "Finite simulator acquisition complete";
    error.clear();
    return FrameWaitResult::Stopped;
  }

  fillFrame(frame, next_frame_id_++);
  ++telemetry_.frames_received;
  if (telemetry_.frames_received % config_.digitizer.records_per_buffer == 0U) {
    ++telemetry_.dma_buffers_received;
  }
  error.clear();
  return FrameWaitResult::FrameReady;
}

bool FakeDigitizer::abort(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  aborted_ = true;
  telemetry_.device.running = false;
  telemetry_.device.detail = "Simulator acquisition aborted";
  error.clear();
  return true;
}

bool FakeDigitizer::stop(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  telemetry_.device.running = false;
  telemetry_.device.ready = telemetry_.device.connected;
  telemetry_.device.detail = telemetry_.device.connected ? "Simulator ready" : "Simulator disconnected";
  error.clear();
  return true;
}

void FakeDigitizer::fillFrame(RawFrame& frame, std::uint64_t frame_id) const {
  frame = {};
  frame.samples.assign(config_.digitizer.sample_point, 0);
  const auto& up = config_.chirp_segmentation.up_segment;
  const auto& down = config_.chirp_segmentation.down_segment;
  const double phase = static_cast<double>(frame_id % 32U) * 0.03;
  for (std::uint32_t index = 0; index < up.length(); ++index) {
    frame.samples[up.start_sample + index] = tone(index, up.length(), 37.0, 12000.0, phase);
  }
  for (std::uint32_t index = 0; index < down.length(); ++index) {
    frame.samples[down.start_sample + index] = tone(index, down.length(), 43.0, 10000.0, -phase);
  }

  auto& metadata = frame.metadata;
  metadata.frame_kind = FrameKind::FullChirpPeriod;
  metadata.frame_id = frame_id;
  metadata.dma_buffer_sequence = (frame_id - 1U) / config_.digitizer.records_per_buffer;
  metadata.record_index_in_buffer = static_cast<std::uint32_t>(
      (frame_id - 1U) % config_.digitizer.records_per_buffer);
  metadata.records_in_buffer = config_.digitizer.records_per_buffer;
  metadata.host_timestamp_ns = nowNs();
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
