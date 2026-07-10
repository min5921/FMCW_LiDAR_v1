#include "core/acquisition_session.h"

#include "core/config_validation.h"

#include <chrono>
#include <sstream>
#include <string_view>
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

void stampScanPosition(const SystemConfig& config, RawFrame& frame) {
  const auto zero_based = frame.metadata.frame_id == 0 ? 0 : frame.metadata.frame_id - 1;
  const auto y = static_cast<std::uint32_t>((zero_based / config.scan.x_pixel_count) % config.scan.y_line_count);
  auto x = static_cast<std::uint32_t>(zero_based % config.scan.x_pixel_count);
  if (config.scan.bidirectional && (y % 2U) != 0U) {
    x = config.scan.x_pixel_count - 1U - x;
  }
  frame.metadata.scan_position.x_index = x;
  frame.metadata.scan_position.y_index = y;
  frame.metadata.scan_position.x_angle_deg = static_cast<float>(config.scan.x_start_deg +
      (config.scan.x_end_deg - config.scan.x_start_deg) * static_cast<double>(x) /
          static_cast<double>(config.scan.x_pixel_count - 1U));
  frame.metadata.scan_position.y_angle_deg = static_cast<float>(config.scan.y_start_deg +
      (config.scan.y_end_deg - config.scan.y_start_deg) * static_cast<double>(y) /
          static_cast<double>(config.scan.y_line_count - 1U));
  frame.metadata.scan_position.valid = true;
}

}  // namespace

AcquisitionSession::AcquisitionSession(IDigitizer& digitizer, IEdfaController& edfa, IMcuController& mcu)
    : digitizer_(digitizer), edfa_(edfa), mcu_(mcu) {}

AcquisitionSession::~AcquisitionSession() {
  std::string ignored;
  stopDevices(true, ignored);
  disconnect();
}

bool AcquisitionSession::configure(const SystemConfig& config, std::uint64_t config_revision, std::string& error) {
  if (running_) {
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
  config_revision_ = config_revision;
  configured_ = true;
  error.clear();
  return true;
}

bool AcquisitionSession::connect(std::string& error) {
  if (!configured_) {
    error = "Configure the acquisition session before connecting devices";
    return false;
  }
  if (connected_) {
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
  connected_ = true;
  error.clear();
  return true;
}

void AcquisitionSession::disconnect() {
  if (running_) {
    std::string ignored;
    stopDevices(true, ignored);
  }
  mcu_.disconnect();
  edfa_.disconnect();
  digitizer_.disconnect();
  connected_ = false;
}

bool AcquisitionSession::start(std::string& error) {
  if (!configured_ || !connected_ || running_) {
    error = "Acquisition session is not configured and connected, or is already running";
    return false;
  }

  if (config_.edfa.mode == EdfaMode::Controlled) {
    if (!edfa_.setControlMode(config_.edfa.control_mode, error) ||
        !edfa_.setOutputSetpoint(config_.edfa.output_setpoint, error) ||
        !edfa_.setOutputEnabled(true, error)) {
      std::string ignored;
      edfa_.emergencyOff(ignored);
      return false;
    }
  }
  if (config_.mcu.enabled && !mcu_.startScan(error)) {
    std::string ignored;
    edfa_.emergencyOff(ignored);
    return false;
  }
  if (!digitizer_.start(error)) {
    std::string ignored;
    mcu_.emergencyStop(ignored);
    edfa_.emergencyOff(ignored);
    return false;
  }

  running_ = true;
  error.clear();
  return true;
}

FrameWaitResult AcquisitionSession::waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout,
                                                  std::string& error) {
  if (!running_) {
    error = "Acquisition is not running";
    return FrameWaitResult::Stopped;
  }
  const auto result = digitizer_.waitForFrame(frame, timeout, error);
  if (result != FrameWaitResult::FrameReady) {
    if (result == FrameWaitResult::Stopped) {
      running_ = false;
    }
    return result;
  }
  if (frame.metadata.frame_kind != FrameKind::FullChirpPeriod ||
      frame.metadata.channel != config_.digitizer.channel ||
      frame.samples.size() != config_.digitizer.sample_point ||
      !frame.metadata.up_segment.validFor(config_.digitizer.sample_point) ||
      !frame.metadata.down_segment.validFor(config_.digitizer.sample_point)) {
    error = "Digitizer returned a frame that violates the full-period single-channel contract";
    return FrameWaitResult::Error;
  }

  const auto edfa_status = edfa_.status();
  frame.metadata.config_revision = config_revision_;
  frame.metadata.optical_state.edfa_used = config_.edfa.mode != EdfaMode::None;
  frame.metadata.optical_state.edfa_output_enabled = edfa_status.output_enabled;
  frame.metadata.optical_state.laser_enabled = true;
  frame.metadata.optical_state.revision = config_revision_;
  stampScanPosition(config_, frame);
  error.clear();
  return FrameWaitResult::FrameReady;
}

bool AcquisitionSession::stop(std::string& error) {
  return stopDevices(false, error);
}

bool AcquisitionSession::emergencyStop(std::string& error) {
  return stopDevices(true, error);
}

bool AcquisitionSession::configured() const { return configured_; }

bool AcquisitionSession::connected() const { return connected_; }

bool AcquisitionSession::running() const { return running_; }

AcquisitionTelemetrySnapshot AcquisitionSession::telemetry() const {
  AcquisitionTelemetrySnapshot snapshot;
  snapshot.timestamp_ns = nowNs();
  snapshot.config_revision = config_revision_;
  snapshot.configured = configured_;
  snapshot.connected = connected_;
  snapshot.running = running_;
  snapshot.digitizer = digitizer_.telemetry();
  snapshot.edfa = edfa_.status();
  snapshot.mcu = mcu_.status();
  return snapshot;
}

bool AcquisitionSession::stopDevices(bool emergency, std::string& error) {
  bool success = true;
  std::string device_error;

  if (running_ || digitizer_.telemetry().device.running) {
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
  if (config_.mcu.enabled) {
    device_error.clear();
    const bool stopped = emergency ? mcu_.emergencyStop(device_error) : mcu_.stopScan(device_error);
    if (!stopped) {
      appendError(error, "MCU stop", device_error);
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
  running_ = false;
  if (success) {
    error.clear();
  }
  return success;
}

}  // namespace fmcw
