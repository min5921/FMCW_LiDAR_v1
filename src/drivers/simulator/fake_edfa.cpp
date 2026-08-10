#include "drivers/simulator/fake_edfa.h"

#include <cmath>

namespace fmcw {
namespace {

double toDbm(const OpticalPowerSetpoint& setpoint) {
  if (setpoint.unit == OpticalPowerUnit::Dbm) {
    return setpoint.value;
  }
  return setpoint.value > 0.0 ? 10.0 * std::log10(setpoint.value) : -INFINITY;
}

}  // namespace

std::string FakeEdfaController::name() const { return "Fake optional EDFA"; }

EdfaStatus FakeEdfaController::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

bool FakeEdfaController::configure(const SystemConfig& config, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.device.running) {
    error = "Cannot configure fake EDFA while output is enabled";
    return false;
  }
  config_ = config.edfa;
  status_ = {};
  status_.mode = config_.mode;
  status_.bypassed = config_.mode == EdfaMode::None;
  status_.required_before_start = config_.required_before_start;
  status_.control_mode = config_.control_mode;
  status_.setpoint = config_.output_setpoint;
  status_.device.detail = status_.bypassed ? "EDFA disabled by profile" : "EDFA simulator configured";
  configured_ = true;
  error.clear();
  return true;
}

bool FakeEdfaController::connect(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_) {
    error = "Configure the fake EDFA before connecting";
    return false;
  }
  status_.device.connected = config_.mode != EdfaMode::None;
  status_.device.ready = true;
  status_.interlock_closed = config_.mode != EdfaMode::None;
  status_.device.detail = config_.mode == EdfaMode::None ? "EDFA bypass active" : "EDFA simulator ready";
  error.clear();
  return true;
}

void FakeEdfaController::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.device.connected = false;
  status_.device.ready = config_.mode == EdfaMode::None;
  status_.device.running = false;
  status_.output_enabled = false;
  status_.device.detail = config_.mode == EdfaMode::None ? "EDFA bypass active" : "EDFA simulator disconnected";
}

bool FakeEdfaController::pollStatus(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (config_.mode == EdfaMode::Controlled && !status_.device.connected) {
    status_.telemetry_valid = false;
    error = "Simulated EDFA is disconnected";
    return false;
  }
  status_.telemetry_valid = config_.mode == EdfaMode::Controlled;
  error.clear();
  return true;
}

bool FakeEdfaController::setControlMode(EdfaControlMode mode, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (config_.mode != EdfaMode::Controlled || !status_.device.connected) {
    error = "EDFA control mode requires a connected controlled profile";
    return false;
  }
  status_.control_mode = mode;
  error.clear();
  return true;
}

bool FakeEdfaController::setOutputSetpoint(const OpticalPowerSetpoint& setpoint, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const double dbm = toDbm(setpoint);
  if (config_.mode != EdfaMode::Controlled || !status_.device.connected || !std::isfinite(dbm) ||
      dbm < config_.output_min_dbm || dbm > config_.output_max_dbm) {
    error = "EDFA setpoint is unavailable or outside the safe output range";
    return false;
  }
  status_.setpoint = setpoint;
  status_.measured_output_dbm = dbm;
  error.clear();
  return true;
}

bool FakeEdfaController::setOutputEnabled(bool enabled, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (config_.mode != EdfaMode::Controlled || !status_.device.connected) {
    error = "EDFA output command requires a connected controlled profile";
    return false;
  }
  status_.output_enabled = enabled;
  status_.device.running = enabled;
  status_.device.detail = enabled ? "Simulated EDFA output enabled" : "Simulated EDFA output disabled";
  error.clear();
  return true;
}

bool FakeEdfaController::resetAlarm(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.alarm_active = false;
  status_.alarm_code.clear();
  error.clear();
  return true;
}

bool FakeEdfaController::emergencyOff(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.output_enabled = false;
  status_.device.running = false;
  status_.device.detail = config_.mode == EdfaMode::None ? "EDFA bypass active" : "EDFA emergency off";
  error.clear();
  return true;
}

}  // namespace fmcw
