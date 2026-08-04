#include "drivers/simulator/fake_mcu.h"

#include "drivers/mcu/mcu_protocol.h"

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
  waveform_sample_rate_hz_ = config.scan.scanner_sample_rate_hz;
  loaded_waveform_.reset();
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
  status_.waveform_points = 0;
  status_.marker_rising_edges = 0;
  status_.waveform_sample_rate_hz = 0.0;
  status_.scan_enabled = false;
  loaded_waveform_.reset();
  status_.last_ack.clear();
  status_.device.detail = config_.enabled ? "MCU simulator disconnected" : "MCU bypass active";
}

bool FakeMcuController::uploadWaveform(const std::vector<McuWaveformFrame>& frames, std::string& error,
                                       const McuUploadProgressCallback& progress) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto total_points = static_cast<std::uint32_t>(frames.size());
  if (!config_.enabled || !status_.device.connected || status_.scan_enabled || frames.empty() || frames.size() > 15000U) {
    error = "MCU waveform upload requires a connected idle MCU and 1..15000 points";
    if (progress) {
      progress(McuUploadProgress{McuUploadStage::Failed, 0, total_points, error});
    }
    return false;
  }
  const auto pending_waveform = McuProtocol::snapshotForUploadedWaveform(
      frames, waveform_sample_rate_hz_);
  if (!pending_waveform) {
    error = "MCU waveform requires finite X/Y commands and at least one B-trigger marker";
    if (progress) {
      progress(McuUploadProgress{McuUploadStage::Failed, 0, total_points, error});
    }
    return false;
  }
  if (progress) {
    progress(McuUploadProgress{McuUploadStage::Clearing, 0, total_points,
                               "Clearing simulated MCU waveform memory"});
    progress(McuUploadProgress{McuUploadStage::Sending, total_points, total_points,
                               "Sending simulated MCU waveform points"});
    progress(McuUploadProgress{McuUploadStage::Verifying, total_points, total_points,
                               "Verifying simulated MCU waveform"});
  }
  status_.waveform_points = total_points;
  status_.marker_rising_edges = static_cast<std::uint32_t>(pending_waveform->logical_marker_indices.size());
  status_.waveform_sample_rate_hz = pending_waveform->sample_rate_hz;
  loaded_waveform_ = pending_waveform;
  status_.last_ack = "ACK:LOAD_DONE," + std::to_string(frames.size());
  status_.device.ready = true;
  status_.device.detail = "MCU simulator waveform loaded";
  if (progress) {
    progress(McuUploadProgress{McuUploadStage::Complete, total_points, total_points, status_.last_ack});
  }
  error.clear();
  return true;
}

McuWaveformSnapshotPtr FakeMcuController::loadedWaveform() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return loaded_waveform_;
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
  status_.device.ready = !config_.enabled || status_.waveform_points > 0U;
  status_.device.detail = config_.enabled ? "MCU simulator stopped; waveform retained" : "MCU bypass active";
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
