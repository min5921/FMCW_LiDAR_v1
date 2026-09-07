#include "drivers/edfa/edfa_serial_controller.h"

#include <chrono>
#include <cmath>
#include <utility>

namespace fmcw {
namespace {

double toDbm(const OpticalPowerSetpoint& setpoint) {
  if (setpoint.unit == OpticalPowerUnit::Dbm) {
    return setpoint.value;
  }
  return setpoint.value > 0.0 ? 10.0 * std::log10(setpoint.value) : -INFINITY;
}

}  // namespace

EdfaSerialController::EdfaSerialController(std::shared_ptr<ISerialTransport> transport)
    : transport_(std::move(transport)) {}

std::string EdfaSerialController::name() const { return "CivilLaser EDFA serial controller"; }

EdfaStatus EdfaSerialController::status() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

void EdfaSerialController::publishStatus(EdfaStatus next) {
  std::lock_guard<std::mutex> lock(status_mutex_);
  status_ = std::move(next);
}

bool EdfaSerialController::configure(const SystemConfig& config, std::string& error) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (status().output_enabled) {
    error = "Disable EDFA output before reconfiguration";
    return false;
  }
  config_ = config.edfa;
  EdfaStatus next;
  next.mode = config_.mode;
  next.bypassed = config_.mode == EdfaMode::None;
  next.required_before_start = config_.required_before_start;
  next.control_mode = config_.control_mode;
  next.setpoint = config_.output_setpoint;
  next.device.ready = config_.mode != EdfaMode::Controlled;
  next.device.detail = config_.mode == EdfaMode::None ? "EDFA bypass active" :
                          config_.mode == EdfaMode::Manual ? "EDFA manual mode" : "EDFA serial configured";
  publishStatus(std::move(next));
  configured_ = true;
  error.clear();
  return true;
}

bool EdfaSerialController::connect(std::string& error) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (!configured_) {
    error = "Configure the EDFA before connecting";
    return false;
  }
  auto next = status();
  if (config_.mode != EdfaMode::Controlled) {
    next.device.connected = false;
    next.device.ready = true;
    next.device.detail = config_.mode == EdfaMode::None ? "EDFA bypass active" : "EDFA manual operator control";
    publishStatus(std::move(next));
    error.clear();
    return true;
  }
  const SerialSettings settings{config_.port, config_.baud_rate, config_.parity, config_.stop_bits};
  if (!transport_->open(settings, error) || !transport_->purge(error)) {
    transport_->close();
    next.device.connected = false;
    next.device.ready = false;
    next.telemetry_valid = false;
    next.device.detail = "EDFA connection failed: " + error;
    publishStatus(std::move(next));
    return false;
  }
  if (!refreshDeviceState(next, error)) {
    transport_->close();
    next.device.connected = false;
    next.device.ready = false;
    next.telemetry_valid = false;
    next.device.detail = "EDFA connection failed: " + error;
    publishStatus(std::move(next));
    return false;
  }
  next.device.connected = true;
  next.device.ready = true;
  next.device.detail = next.output_enabled
      ? "EDFA connected | output enabled"
      : "EDFA connected | output disabled";
  publishStatus(std::move(next));
  return true;
}

void EdfaSerialController::disconnect() {
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (config_.mode == EdfaMode::Controlled && status().output_enabled && transport_->isOpen()) {
    std::string ignored;
    setOutputEnabledLocked(false, ignored);
  }

  transport_->close();
  auto next = status();
  next.device.connected = false;
  next.device.running = false;
  next.output_enabled = false;
  next.telemetry_valid = false;
  next.device.ready = config_.mode != EdfaMode::Controlled;
  next.device.detail = config_.mode == EdfaMode::None ? "EDFA bypass active" : "EDFA disconnected";
  publishStatus(std::move(next));
}

bool EdfaSerialController::pollStatus(std::string& error) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (config_.mode != EdfaMode::Controlled) {
    error.clear();
    return true;
  }
  auto next = status();
  if (!transport_->isOpen()) {
    next.device.connected = false;
    next.device.ready = false;
    next.telemetry_valid = false;
    next.device.detail = "EDFA status poll failed: serial port is closed";
    publishStatus(std::move(next));
    error = "EDFA serial port is closed";
    return false;
  }
  if (!refreshDeviceState(next, error)) {
    next.device.ready = false;
    next.telemetry_valid = false;
    next.device.detail = "EDFA status poll failed: " + error;
    publishStatus(std::move(next));
    return false;
  }
  next.device.connected = true;
  next.device.ready = true;
  next.device.detail = next.output_enabled
      ? "EDFA connected | output enabled"
      : "EDFA connected | output disabled";
  publishStatus(std::move(next));
  error.clear();
  return true;
}

bool EdfaSerialController::setControlMode(EdfaControlMode mode, std::string& error) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (config_.mode != EdfaMode::Controlled || !transport_->isOpen()) {
    error = "EDFA mode command requires a controlled serial connection";
    return false;
  }
  EdfaPacket response;
  EdfaControlMode confirmed = EdfaControlMode::Apc;
  if (!transact(EdfaProtocol::setMode(mode), response, error) ||
      !EdfaProtocol::decodeMode(response, confirmed, error)) {
    return false;
  }
  auto next = status();
  next.control_mode = confirmed;
  publishStatus(std::move(next));
  if (confirmed != mode) {
    error = "EDFA confirmed a different control mode";
    return false;
  }
  error.clear();
  return true;
}

bool EdfaSerialController::setOutputSetpoint(const OpticalPowerSetpoint& setpoint, std::string& error) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  auto next = status();
  const double dbm = toDbm(setpoint);
  if (config_.mode != EdfaMode::Controlled || !transport_->isOpen() || next.control_mode != EdfaControlMode::Apc) {
    error = "The current EDFA schema supports serial output setpoint only in APC mode";
    return false;
  }
  if (!std::isfinite(dbm) || dbm < config_.output_min_dbm || dbm > config_.output_max_dbm) {
    error = "EDFA output setpoint is outside the configured safe range";
    return false;
  }
  const auto command = EdfaProtocol::setTargetPowerDbm(dbm);
  if (command.empty()) {
    error = "EDFA output setpoint cannot be encoded";
    return false;
  }
  EdfaPacket response;
  double confirmed_dbm = 0.0;
  if (!transact(command, response, error) || !EdfaProtocol::decodePowerDbm(response, confirmed_dbm, error)) {
    return false;
  }
  if (std::abs(confirmed_dbm - dbm) > 0.02) {
    error = "EDFA confirmed a different output setpoint";
    return false;
  }
  next.setpoint = {confirmed_dbm, OpticalPowerUnit::Dbm};
  publishStatus(std::move(next));
  error.clear();
  return true;
}

bool EdfaSerialController::setOutputEnabled(bool enabled, std::string& error) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  return setOutputEnabledLocked(enabled, error);
}

bool EdfaSerialController::setOutputEnabledLocked(bool enabled, std::string& error) {
  if (config_.mode != EdfaMode::Controlled || !transport_->isOpen()) {
    error = "EDFA activation command requires a controlled serial connection";
    return false;
  }
  EdfaPacket response;
  bool confirmed = false;
  if (!transact(EdfaProtocol::setActivation(enabled), response, error) ||
      !EdfaProtocol::decodeActivation(response, confirmed, error)) {
    return false;
  }
  if (confirmed != enabled) {
    error = enabled
        ? "EDFA rejected activation; verify the front-panel key is ON and the device is in normal state"
        : "EDFA shutdown state was not confirmed";
    return false;
  }
  auto next = status();
  next.output_enabled = enabled;
  next.device.running = enabled;
  next.device.detail = enabled ? "EDFA output enabled" : "EDFA output disabled";
  // Publish the confirmed activation before waiting for optional telemetry.
  publishStatus(next);
  std::string telemetry_error;
  const bool telemetry_refreshed = refreshReading(next, telemetry_error);
  if (!telemetry_refreshed) {
    next.telemetry_valid = false;
    next.device.detail += " | telemetry refresh failed: " + telemetry_error;
  }
  publishStatus(std::move(next));
  error.clear();
  return true;
}

bool EdfaSerialController::resetAlarm(std::string& error) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (config_.mode != EdfaMode::Controlled || !transport_->isOpen()) {
    error = "EDFA alarm refresh requires a controlled serial connection";
    return false;
  }
  auto next = status();
  if (!refreshReading(next, error)) {
    next.telemetry_valid = false;
    publishStatus(std::move(next));
    return false;
  }
  next.alarm_active = false;
  next.alarm_code.clear();
  publishStatus(std::move(next));
  return true;
}

bool EdfaSerialController::emergencyOff(std::string& error) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (config_.mode != EdfaMode::Controlled) {
    auto next = status();
    next.output_enabled = false;
    next.device.running = false;
    publishStatus(std::move(next));
    error.clear();
    return true;
  }
  return setOutputEnabledLocked(false, error);
}

bool EdfaSerialController::transact(const std::vector<std::uint8_t>& command, EdfaPacket& response,
                                    std::string& error) {
  if (command.empty() || !transport_->write(command, error)) {
    return false;
  }
  std::vector<std::uint8_t> prefix;
  if (!transport_->readExact(3, prefix, std::chrono::milliseconds(config_.timeout_ms), error)) {
    return false;
  }
  if (prefix.size() != 3U || prefix[0] != 0xED || prefix[1] != 0xFA || prefix[2] < 2U) {
    error = "EDFA returned an invalid response prefix";
    return false;
  }
  std::vector<std::uint8_t> remainder;
  if (!transport_->readExact(prefix[2], remainder, std::chrono::milliseconds(config_.timeout_ms), error)) {
    return false;
  }
  prefix.insert(prefix.end(), remainder.begin(), remainder.end());
  return EdfaProtocol::parseResponse(prefix, response, error);
}

bool EdfaSerialController::refreshReading(EdfaStatus& next, std::string& error) {
  EdfaPacket response;
  EdfaDeviceReading reading;
  if (!transact(EdfaProtocol::queryStatus(), response, error) ||
      !EdfaProtocol::decodeStatus(response, reading, error)) {
    return false;
  }
  next.measured_output_dbm = reading.output_power_dbm;
  next.measured_input_dbm = reading.input_power_dbm;
  next.measured_current_ma = reading.current_ma;
  next.telemetry_valid = true;
  next.telemetry_timestamp_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count());
  error.clear();
  return true;
}

bool EdfaSerialController::refreshDeviceState(EdfaStatus& next, std::string& error) {
  auto confirmed = next;
  if (!refreshReading(confirmed, error)) {
    return false;
  }

  EdfaPacket response;
  if (!transact(EdfaProtocol::queryMode(), response, error) ||
      !EdfaProtocol::decodeMode(response, confirmed.control_mode, error)) {
    return false;
  }

  double target_dbm = 0.0;
  if (!transact(EdfaProtocol::queryTargetPower(), response, error) ||
      !EdfaProtocol::decodePowerDbm(response, target_dbm, error)) {
    return false;
  }
  confirmed.setpoint = {target_dbm, OpticalPowerUnit::Dbm};

  bool output_enabled = false;
  if (!transact(EdfaProtocol::queryActivation(), response, error) ||
      !EdfaProtocol::decodeActivation(response, output_enabled, error)) {
    return false;
  }
  confirmed.output_enabled = output_enabled;
  confirmed.device.running = output_enabled;
  next = std::move(confirmed);
  error.clear();
  return true;
}

}  // namespace fmcw
