#include "drivers/simulator/fake_mcu.h"

namespace fmcw {

std::string FakeMcuController::name() const { return "Fake MEMS MCU"; }

McuStatus FakeMcuController::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

bool FakeMcuController::configure(const SystemConfig& config, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.scan_enabled) {
    error = "Cannot configure fake MCU while scan output is active";
    return false;
  }
  config_ = config.mcu;
  status_ = {};
  status_.device.detail = config_.enabled ? "MCU simulator configured" : "MCU disabled by profile";
  configured_ = true;
  error.clear();
  return true;
}

bool FakeMcuController::connect(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_) {
    error = "Configure the fake MCU before connecting";
    return false;
  }
  status_.device.connected = config_.enabled;
  status_.device.ready = !config_.enabled;
  status_.device.detail = config_.enabled ? "MCU simulator connected" : "MCU bypass active";
  error.clear();
  return true;
}

void FakeMcuController::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.device.connected = false;
  status_.device.ready = !config_.enabled;
  status_.device.running = false;
  status_.scan_enabled = false;
  status_.device.detail = config_.enabled ? "MCU simulator disconnected" : "MCU bypass active";
}

bool FakeMcuController::uploadWaveform(const std::vector<McuWaveformFrame>& frames, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!config_.enabled || !status_.device.connected || status_.scan_enabled || frames.empty() || frames.size() > 15000U) {
    error = "MCU waveform upload requires a connected idle MCU and 1..15000 points";
    return false;
  }
  status_.waveform_points = static_cast<std::uint32_t>(frames.size());
  status_.last_ack = "ACK:LOAD_DONE," + std::to_string(frames.size());
  status_.device.ready = true;
  status_.device.detail = "MCU simulator waveform loaded";
  error.clear();
  return true;
}

bool FakeMcuController::startScan(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!config_.enabled || !status_.device.connected || !status_.device.ready || status_.waveform_points == 0) {
    error = "MCU scan requires a loaded waveform on a connected device";
    return false;
  }
  status_.scan_enabled = true;
  status_.device.running = true;
  status_.last_ack = "ACK:START";
  status_.device.detail = "MCU simulator scan active";
  error.clear();
  return true;
}

bool FakeMcuController::stopScan(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.scan_enabled = false;
  status_.device.running = false;
  status_.last_ack = "ACK:STOP";
  status_.device.detail = config_.enabled ? "MCU simulator stopped" : "MCU bypass active";
  error.clear();
  return true;
}

bool FakeMcuController::emergencyStop(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.scan_enabled = false;
  status_.device.running = false;
  status_.last_ack = "LOCAL:EMERGENCY_STOP";
  status_.device.detail = config_.enabled ? "MCU simulator emergency stop" : "MCU bypass active";
  error.clear();
  return true;
}

}  // namespace fmcw
