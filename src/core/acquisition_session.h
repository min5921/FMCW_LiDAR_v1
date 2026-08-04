#pragma once

#include "core/device_interfaces.h"
#include "core/system_telemetry.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace fmcw {

class AcquisitionSession {
 public:
  AcquisitionSession(IDigitizer& digitizer, IEdfaController& edfa, IMcuController& mcu);
  ~AcquisitionSession();

  bool configure(const SystemConfig& config, std::uint64_t config_revision, std::string& error);
  bool connect(std::string& error);
  void disconnect();
  bool start(std::string& error);
  FrameWaitResult waitForBatch(RawFrameBatchPtr& batch, std::chrono::milliseconds timeout,
                               std::string& error);
  FrameWaitResult waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout, std::string& error);
  bool stop(std::string& error);
  bool emergencyStop(std::string& error);

  bool configured() const;
  bool connected() const;
  bool running() const;
  AcquisitionTelemetrySnapshot telemetry() const;

 private:
  bool stopDevices(bool emergency, std::string& error);

  IDigitizer& digitizer_;
  IEdfaController& edfa_;
  IMcuController& mcu_;
  SystemConfig config_;
  McuWaveformSnapshotPtr active_waveform_;
  std::uint64_t scan_line_sequence_ = 0;
  std::atomic_uint64_t config_revision_{0};
  std::atomic_bool configured_{false};
  std::atomic_bool connected_{false};
  std::atomic_bool running_{false};
};

}  // namespace fmcw
