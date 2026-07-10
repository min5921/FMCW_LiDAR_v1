#include "drivers/edfa/edfa_serial_controller.h"

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
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

bool EdfaSerialController::configure(const SystemConfig& config, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.output_enabled) {
    error = "Disable EDFA output before reconfiguration";
    return false;
  }
  config_ = config.edfa;
  status_ = {};
  status_.mode = config_.mode;
  status_.bypassed = config_.mode == EdfaMode::None;
  status_.required_before_start = config_.required_before_start;
  status_.control_mode = config_.control_mode;
  status_.setpoint = config_.output_setpoint;
  status_.device.ready = config_.mode != EdfaMode::Controlled;
  status_.device.detail = config_.mode == EdfaMode::None ? "EDFA bypass active" :
                          config_.mode == EdfaMode::Manual ? "EDFA manual mode" : "EDFA serial configured";
  configured_ = true;
  error.clear();
  return true;
}

bool EdfaSerialController::connect(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_) {
    error = "Configure the EDFA before connecting";
    return false;
  }
  if (config_.mode != EdfaMode::Controlled) {
    status_.device.connected = false;
    status_.device.ready = true;
    status_.device.detail = config_.mode == EdfaMode::None ? "EDFA bypass active" : "EDFA manual operator control";
    error.clear();
    return true;
  }
  const SerialSettings settings{config_.port, config_.baud_rate, config_.parity, config_.stop_bits};
  if (!transport_->open(settings, error) || !transport_->purge(error)) {
    transport_->close();
    return false;
  }
  status_.device.connected = true;
  status_.device.ready = false;
  if (!refreshReading(error)) {
    transport_->close();
    status_.device.connected = false;
    return false;
  }
  status_.device.ready = true;
  status_.device.detail = "EDFA connected and responding";
  return true;
}

void EdfaSerialController::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  transport_->close();
  status_.device.connected = false;
  status_.device.running = false;
  status_.output_enabled = false;
  status_.device.ready = config_.mode != EdfaMode::Controlled;
  status_.device.detail = config_.mode == EdfaMode::None ? "EDFA bypass active" : "EDFA disconnected";
}

bool EdfaSerialController::setControlMode(EdfaControlMode mode, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (config_.mode != EdfaMode::Controlled || !transport_->isOpen()) {
    error = "EDFA mode command requires a controlled serial connection";
    return false;
  }
  EdfaPacket response;
  if (!transact(EdfaProtocol::setMode(mode), response, error) ||
      !EdfaProtocol::decodeMode(response, status_.control_mode, error)) {
    return false;
  }
  return status_.control_mode == mode;
}

bool EdfaSerialController::setOutputSetpoint(const OpticalPowerSetpoint& setpoint, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const double dbm = toDbm(setpoint);
  if (config_.mode != EdfaMode::Controlled || !transport_->isOpen() || status_.control_mode != EdfaControlMode::Apc) {
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
  status_.setpoint = setpoint;
  return true;
}

bool EdfaSerialController::setOutputEnabled(bool enabled, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
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
    error = "EDFA activation state was not confirmed";
    return false;
  }
  status_.output_enabled = enabled;
  status_.device.running = enabled;
  status_.device.detail = enabled ? "EDFA output enabled" : "EDFA output disabled";
  return true;
}

bool EdfaSerialController::resetAlarm(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (config_.mode != EdfaMode::Controlled || !transport_->isOpen()) {
    error = "EDFA alarm refresh requires a controlled serial connection";
    return false;
  }
  if (!refreshReading(error)) {
    return false;
  }
  status_.alarm_active = false;
  status_.alarm_code.clear();
  return true;
}

bool EdfaSerialController::emergencyOff(std::string& error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config_.mode != EdfaMode::Controlled) {
      status_.output_enabled = false;
      status_.device.running = false;
      error.clear();
      return true;
    }
  }
  return setOutputEnabled(false, error);
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

bool EdfaSerialController::refreshReading(std::string& error) {
  EdfaPacket response;
  EdfaDeviceReading reading;
  if (!transact(EdfaProtocol::queryStatus(), response, error) ||
      !EdfaProtocol::decodeStatus(response, reading, error)) {
    return false;
  }
  status_.measured_output_dbm = reading.output_power_dbm;
  error.clear();
  return true;
}

}  // namespace fmcw
