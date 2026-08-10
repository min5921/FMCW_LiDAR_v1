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
  const bool retain_waveform = configured_ && loaded_waveform_ &&
      mcuWaveformContractEquivalent(system_config_, config);
  config_ = config.mcu;
  system_config_ = config;
  waveform_sample_rate_hz_ = config.scan.scanner_sample_rate_hz;
  const auto retained_waveform = retain_waveform ? loaded_waveform_ : McuWaveformSnapshotPtr{};
  const auto retained_points = retain_waveform ? status_.waveform_points : 0U;
  const auto retained_edges = retain_waveform ? status_.marker_rising_edges : 0U;
  const auto retained_rate = retain_waveform ? status_.waveform_sample_rate_hz : 0.0;
  loaded_waveform_ = retained_waveform;
  status_ = {};
  status_.waveform_points = retained_points;
  status_.marker_rising_edges = retained_edges;
  status_.waveform_sample_rate_hz = retained_rate;
  status_.device.detail = !config_.enabled
      ? "MCU disabled by profile"
      : retain_waveform ? "MCU simulator configured; waveform retained"
                        : "MCU simulator configured";
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
  status_.device.ready = !config_.enabled ||
      (loaded_waveform_ && loaded_waveform_->valid() && status_.waveform_points > 0U);
  status_.device.detail = !config_.enabled
      ? "MCU bypass active"
      : status_.device.ready ? "MCU simulator connected; waveform retained"
                             : "MCU simulator connected";
  error.clear();
  return true;
}

void FakeMcuController::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.device.connected = false;
  status_.device.ready = !config_.enabled;
  status_.device.running = false;
  status_.scan_enabled = false;
  status_.last_ack.clear();
  status_.device.detail = !config_.enabled
      ? "MCU bypass active"
      : loaded_waveform_ ? "MCU simulator disconnected; waveform retained"
                         : "MCU simulator disconnected";
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
