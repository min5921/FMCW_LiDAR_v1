#include "core/acquisition_session.h"

#include "core/config_validation.h"
#include "core/scan_trajectory.h"

#include <chrono>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

namespace fmcw {
namespace {

std::uint64_t nowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

void appendError(std::string& destination, std::string_view source, const std::string& message) {
  if (message.empty()) {
    return;
  }
  if (!destination.empty()) {
    destination += "; ";
  }
  destination += std::string(source) + ": " + message;
}

bool validateAndStampFrame(const SystemConfig& config, std::uint64_t config_revision,
                           const EdfaStatus& edfa_status, RawFrame& frame, std::string& error) {
  if (frame.metadata.frame_kind != FrameKind::FullChirpPeriod ||
      frame.metadata.channel != config.digitizer.channel ||
      frame.samples.size() != config.digitizer.sample_point ||
      !frame.metadata.up_segment.validFor(config.digitizer.sample_point) ||
      !frame.metadata.down_segment.validFor(config.digitizer.sample_point)) {
    error = "Digitizer returned a frame that violates the full-period single-channel contract";
    return false;
  }
  frame.metadata.config_revision = config_revision;
  frame.metadata.optical_state.edfa_used = config.edfa.mode != EdfaMode::None;
  frame.metadata.optical_state.edfa_output_enabled = edfa_status.output_enabled;
  frame.metadata.optical_state.laser_enabled = true;
  frame.metadata.optical_state.revision = config_revision;
  return true;
}

}  // namespace

AcquisitionSession::AcquisitionSession(IDigitizer& digitizer, IEdfaController& edfa, IMcuController& mcu)
    : digitizer_(digitizer), edfa_(edfa), mcu_(mcu) {}

AcquisitionSession::~AcquisitionSession() { disconnect(); }

bool AcquisitionSession::configure(const SystemConfig& config, std::uint64_t config_revision, std::string& error) {
  if (armed_.load() || running_.load()) {
    error = "Cannot configure devices while acquisition is running";
    return false;
  }
  const auto validation = ConfigValidator::validate(config);
  if (validation.hasErrors()) {
    error = "Configuration validation failed before device setup";
    return false;
  }
  if (!digitizer_.configure(config, error)) {
    return false;
  }
  if (!edfa_.configure(config, error)) {
    return false;
  }
  if (!mcu_.configure(config, error)) {
    return false;
  }
  config_ = config;
  active_waveform_.reset();
  scan_line_sequence_ = 0U;
  config_revision_.store(config_revision);
  configured_.store(true);
  error.clear();
  return true;
}

bool AcquisitionSession::connect(std::string& error) {
  if (!configured_.load()) {
    error = "Configure the acquisition session before connecting devices";
    return false;
  }
  if (connected_.load()) {
    return true;
  }
  if (!digitizer_.connect(error)) {
    return false;
  }
  if (!edfa_.connect(error)) {
    digitizer_.disconnect();
    return false;
  }
  if (!mcu_.connect(error)) {
    edfa_.disconnect();
    digitizer_.disconnect();
    return false;
  }
  connected_.store(true);
  error.clear();
  return true;
}

void AcquisitionSession::disconnect() {
  if (armed_.load() || running_.load()) {
    std::string ignored;
    stopDevices(true, ignored);
  } else {
    std::string ignored;
    edfa_.emergencyOff(ignored);
  }
  mcu_.disconnect();
  edfa_.disconnect();
  digitizer_.disconnect();
  connected_.store(false);
  armed_.store(false);
  running_.store(false);
}

bool AcquisitionSession::arm(std::string& error) {
  if (!configured_.load() || !connected_.load() || armed_.load() || running_.load()) {
    error = "Acquisition session is not configured and connected, or is already armed";
    return false;
  }

  active_waveform_.reset();
  scan_line_sequence_ = 0U;
  if (config_.mcu.enabled) {
    active_waveform_ = mcu_.loadedWaveform();
    if (!active_waveform_ || !active_waveform_->valid() ||
        active_waveform_->logical_marker_indices.size() != config_.scan.y_line_count ||
        active_waveform_->emitted_marker_indices.size() != config_.scan.y_line_count) {
      error = "Loaded MCU waveform does not provide one logical and emitted B-trigger per configured scan line";
      return false;
    }
  }

  if (config_.edfa.mode == EdfaMode::Controlled) {
    if (!edfa_.setControlMode(config_.edfa.control_mode, error) ||
        !edfa_.setOutputSetpoint(config_.edfa.output_setpoint, error) ||
        !edfa_.setOutputEnabled(true, error)) {
      std::string ignored;
      edfa_.emergencyOff(ignored);
      return false;
    }
    if (config_.edfa.warmup_delay_ms > 0U) {
      std::this_thread::sleep_for(std::chrono::milliseconds(config_.edfa.warmup_delay_ms));
    }
  }
  // Arm the receiver before the MCU enables the trigger-producing scan waveform.
  if (!digitizer_.start(error)) {
    std::string ignored;
    edfa_.emergencyOff(ignored);
    return false;
  }

  armed_.store(true);
  error.clear();
  return true;
}

bool AcquisitionSession::enableTrigger(std::string& error) {
  if (!armed_.load() || running_.load()) {
    error = "Acquisition session must be armed before enabling its trigger source";
    return false;
  }
  if (config_.mcu.enabled && !mcu_.startScan(error)) {
    std::string ignored;
    digitizer_.abort(ignored);
    digitizer_.stop(ignored);
    edfa_.emergencyOff(ignored);
    armed_.store(false);
    return false;
  }

  running_.store(true);
  error.clear();
  return true;
}

bool AcquisitionSession::start(std::string& error) {
  if (!arm(error)) {
    return false;
  }
  return enableTrigger(error);
}

FrameWaitResult AcquisitionSession::waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout,
                                                  std::string& error) {
  if (!armed_.load()) {
    error = "Acquisition is not running";
    return FrameWaitResult::Stopped;
  }
  const auto result = digitizer_.waitForFrame(frame, timeout, error);
  if (result != FrameWaitResult::FrameReady) {
    if (result == FrameWaitResult::Stopped) {
      running_.store(false);
      armed_.store(false);
    }
    return result;
  }
  if (!validateAndStampFrame(config_, config_revision_.load(), edfa_.status(), frame, error)) {
    return FrameWaitResult::Error;
  }
  if (active_waveform_) {
    TrajectoryLineContext line;
    if (!prepareTrajectoryLine(config_, *active_waveform_, scan_line_sequence_,
                               frame.metadata.records_in_buffer, line) ||
        !mapTrajectoryRecord(config_, *active_waveform_, line,
                             frame.metadata.record_index_in_buffer,
                             frame.metadata.scan_position)) {
      error = "Unable to map the acquired record to the loaded MCU trajectory";
      return FrameWaitResult::Error;
    }
    if (frame.metadata.record_index_in_buffer + 1U >= frame.metadata.records_in_buffer) {
      ++scan_line_sequence_;
    }
  } else if (config_.runtime.acquisition_source == AcquisitionSource::Replay) {
    if (!frame.metadata.scan_position.valid) {
      stampGeneratedRasterPosition(config_, frame);
    }
    if (!alignReplayRasterPosition(config_, frame.metadata.scan_position)) {
      error = "Replay scan position is outside the applied raster geometry";
      return FrameWaitResult::Error;
    }
  } else if (!frame.metadata.scan_position.valid) {
    stampGeneratedRasterPosition(config_, frame);
  }
  error.clear();
  return FrameWaitResult::FrameReady;
}

FrameWaitResult AcquisitionSession::waitForBatch(RawFrameBatchPtr& batch,
                                                 std::chrono::milliseconds timeout,
                                                 std::string& error) {
  batch.reset();
  if (!armed_.load()) {
    error = "Acquisition is not running";
    return FrameWaitResult::Stopped;
  }

  MutableRawFrameBatchPtr mutable_batch;
  const auto result = digitizer_.waitForBatch(mutable_batch, timeout, error);
  if (result != FrameWaitResult::FrameReady) {
    if (result == FrameWaitResult::Stopped) {
      running_.store(false);
      armed_.store(false);
    }
    return result;
  }
  if (!mutable_batch || mutable_batch->records.empty() ||
      mutable_batch->metadata.record_count != mutable_batch->records.size() ||
      mutable_batch->metadata.record_length != config_.digitizer.sample_point) {
    error = "Digitizer returned an invalid or empty DMA batch";
    return FrameWaitResult::Error;
  }

  const auto record_count = static_cast<std::uint32_t>(mutable_batch->records.size());
  const auto edfa_status = edfa_.status();
  const auto config_revision = config_revision_.load();
  const auto line_sequence = scan_line_sequence_;
  TrajectoryLineContext trajectory_line;
  if (active_waveform_ &&
      !prepareTrajectoryLine(config_, *active_waveform_, line_sequence,
                             record_count, trajectory_line)) {
    error = "Unable to prepare the DMA batch trajectory mapping";
    return FrameWaitResult::Error;
  }
  for (std::uint32_t index = 0; index < record_count; ++index) {
    auto& frame = mutable_batch->records[index];
    if (frame.metadata.dma_buffer_sequence != mutable_batch->metadata.sequence ||
        frame.metadata.record_index_in_buffer != index ||
        frame.metadata.records_in_buffer != record_count ||
        !validateAndStampFrame(config_, config_revision, edfa_status, frame, error)) {
      if (error.empty()) {
        error = "Digitizer DMA batch record metadata is inconsistent";
      }
      return FrameWaitResult::Error;
    }
    if (active_waveform_) {
      if (!mapTrajectoryRecord(config_, *active_waveform_, trajectory_line, index,
                               frame.metadata.scan_position)) {
        error = "Unable to map the DMA batch to the loaded MCU trajectory";
        return FrameWaitResult::Error;
      }
    } else if (config_.runtime.acquisition_source == AcquisitionSource::Replay) {
      if (!frame.metadata.scan_position.valid) {
        stampGeneratedRasterPosition(config_, frame);
      }
      if (!alignReplayRasterPosition(config_, frame.metadata.scan_position)) {
        error = "Replay scan position is outside the applied raster geometry";
        return FrameWaitResult::Error;
      }
    } else if (!frame.metadata.scan_position.valid) {
      stampGeneratedRasterPosition(config_, frame);
    }
  }
  ++scan_line_sequence_;
  mutable_batch->metadata.session_ready_timestamp_ns = nowNs();
  batch = std::move(mutable_batch);
  error.clear();
  return FrameWaitResult::FrameReady;
}

bool AcquisitionSession::stop(std::string& error) {
  return stopDevices(false, error);
}

bool AcquisitionSession::emergencyStop(std::string& error) {
  return stopDevices(true, error);
}

bool AcquisitionSession::configured() const { return configured_.load(); }

bool AcquisitionSession::connected() const { return connected_.load(); }

bool AcquisitionSession::armed() const { return armed_.load(); }

bool AcquisitionSession::running() const { return running_.load(); }

AcquisitionTelemetrySnapshot AcquisitionSession::telemetry() const {
  AcquisitionTelemetrySnapshot snapshot;
  snapshot.timestamp_ns = nowNs();
  snapshot.config_revision = config_revision_.load();
  snapshot.configured = configured_.load();
  snapshot.connected = connected_.load();
  snapshot.running = armed_.load();
  snapshot.digitizer = digitizer_.telemetry();
  snapshot.edfa = edfa_.status();
  snapshot.mcu = mcu_.status();
  return snapshot;
}

bool AcquisitionSession::stopDevices(bool emergency, std::string& error) {
  bool success = true;
  std::string device_error;

  // Remove the trigger source before aborting the digitizer DMA path.
  if (config_.mcu.enabled) {
    device_error.clear();
    const bool stopped = emergency ? mcu_.emergencyStop(device_error) : mcu_.stopScan(device_error);
    if (!stopped) {
      appendError(error, "MCU stop", device_error);
      success = false;
    }
  }
  if (armed_.load() || running_.load() || digitizer_.telemetry().device.running) {
    device_error.clear();
    if (!digitizer_.abort(device_error)) {
      appendError(error, "digitizer abort", device_error);
      success = false;
    }
    device_error.clear();
    if (!digitizer_.stop(device_error)) {
      appendError(error, "digitizer stop", device_error);
      success = false;
    }
  }
  if (config_.edfa.mode == EdfaMode::Controlled) {
    device_error.clear();
    const bool disabled = emergency ? edfa_.emergencyOff(device_error) : edfa_.setOutputEnabled(false, device_error);
    if (!disabled) {
      appendError(error, "EDFA off", device_error);
      success = false;
    }
  }
  armed_.store(false);
  running_.store(false);
  if (success) {
    error.clear();
  }
  return success;
}

}  // namespace fmcw
