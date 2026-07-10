#include "drivers/mcu/mcu_serial_controller.h"

#include "drivers/mcu/mcu_protocol.h"

#include <utility>

namespace fmcw {

McuSerialController::McuSerialController(std::shared_ptr<ISerialTransport> transport)
    : transport_(std::move(transport)) {}

std::string McuSerialController::name() const { return "MEMS MCU serial controller"; }

McuStatus McuSerialController::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

bool McuSerialController::configure(const SystemConfig& config, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.device.running) {
    error = "Cannot configure MCU while scan output is active";
    return false;
  }
  config_ = config.mcu;
  status_ = {};
  status_.device.ready = !config_.enabled;
  status_.device.detail = config_.enabled ? "MCU serial configured" : "MCU disabled by profile";
  configured_ = true;
  error.clear();
  return true;
}

bool McuSerialController::connect(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_) {
    error = "Configure the MCU before connecting";
    return false;
  }
  if (!config_.enabled) {
    status_.device.ready = true;
    status_.device.detail = "MCU bypass active";
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
  status_.device.detail = "MCU connected; waveform upload required";
  error.clear();
  return true;
}

void McuSerialController::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  transport_->close();
  status_.device.connected = false;
  status_.device.running = false;
  status_.device.ready = !config_.enabled;
  status_.scan_enabled = false;
  status_.device.detail = config_.enabled ? "MCU disconnected" : "MCU bypass active";
}

bool McuSerialController::uploadWaveform(const std::vector<McuWaveformFrame>& frames, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!config_.enabled || !transport_->isOpen() || status_.scan_enabled || frames.empty() || frames.size() > 15000U) {
    error = "MCU waveform upload requires a connected idle device and 1..15000 points";
    return false;
  }
  if (!sendAndExpect(McuProtocol::clearCommand(), "CLR", std::chrono::milliseconds(config_.timeout_ms), error)) {
    return false;
  }
  for (const auto& frame : frames) {
    if (!transport_->write(bytesFromString(McuProtocol::dataCommand(frame)), error)) {
      status_.device.ready = false;
      return false;
    }
  }
  if (!sendAndExpect(McuProtocol::loadDoneCommand(), "LOAD_DONE",
                     std::chrono::milliseconds(config_.timeout_ms), error)) {
    return false;
  }
  if (!config_.require_ack) {
    status_.waveform_points = static_cast<std::uint32_t>(frames.size());
  }
  if (status_.waveform_points != frames.size()) {
    error = "MCU loaded point count does not match uploaded waveform";
    status_.device.ready = false;
    return false;
  }
  status_.device.ready = true;
  status_.device.detail = "MCU waveform loaded";
  error.clear();
  return true;
}

bool McuSerialController::startScan(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!config_.enabled || !status_.device.connected || !status_.device.ready || status_.waveform_points == 0) {
    error = "MCU scan requires a connected device with a loaded waveform";
    return false;
  }
  if (!sendAndExpect(McuProtocol::startCommand(), "START", std::chrono::milliseconds(config_.timeout_ms), error)) {
    return false;
  }
  status_.scan_enabled = true;
  status_.device.running = true;
  status_.device.detail = "MCU scan active";
  return true;
}

bool McuSerialController::stopScan(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!config_.enabled) {
    error.clear();
    return true;
  }
  if (!transport_->isOpen()) {
    error = "MCU serial port is not open";
    return false;
  }
  const bool acknowledged = sendAndExpect(McuProtocol::stopCommand(), "STOP",
                                          std::chrono::milliseconds(config_.timeout_ms), error);
  status_.scan_enabled = false;
  status_.device.running = false;
  status_.device.detail = acknowledged ? "MCU stopped" : "MCU stop acknowledgement missing";
  return acknowledged;
}

bool McuSerialController::emergencyStop(std::string& error) {
  const bool stopped = stopScan(error);
  if (!stopped) {
    transport_->close();
    std::lock_guard<std::mutex> lock(mutex_);
    status_.device.connected = false;
    status_.device.running = false;
    status_.scan_enabled = false;
    status_.device.detail = "MCU serial closed after emergency stop failure";
  }
  return stopped;
}

bool McuSerialController::sendAndExpect(const std::string& command, std::string_view expected_code,
                                        std::chrono::milliseconds timeout, std::string& error) {
  const auto attempts = config_.retry_count + 1U;
  for (std::uint32_t attempt = 0; attempt < attempts; ++attempt) {
    if (!transport_->write(bytesFromString(command), error)) {
      continue;
    }
    if (!config_.require_ack) {
      return true;
    }
    std::string line;
    if (!transport_->readLine(line, timeout, error)) {
      continue;
    }
    const auto response = McuProtocol::parseResponse(line);
    status_.last_ack = line;
    if (response.acknowledged && response.code == expected_code) {
      if (response.has_count) {
        status_.waveform_points = response.count;
      }
      error.clear();
      return true;
    }
    error = response.error ? response.detail : "Unexpected MCU response: " + line;
  }
  return false;
}

}  // namespace fmcw
