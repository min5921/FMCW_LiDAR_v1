#pragma once

#include "core/device_interfaces.h"
#include "core/system_telemetry.h"

#include <chrono>
#include <cstdint>
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
  std::uint64_t config_revision_ = 0;
  bool configured_ = false;
  bool connected_ = false;
  bool running_ = false;
};

}  // namespace fmcw
