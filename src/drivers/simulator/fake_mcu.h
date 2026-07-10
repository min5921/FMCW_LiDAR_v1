#pragma once

#include "core/device_interfaces.h"

#include <mutex>

namespace fmcw {

class FakeMcuController final : public IMcuController {
 public:
  std::string name() const override;
  McuStatus status() const override;
  bool connect(std::string& error) override;
  void disconnect() override;
  bool configure(const SystemConfig& config, std::string& error) override;
  bool uploadWaveform(const std::vector<McuWaveformFrame>& frames, std::string& error) override;
  bool startScan(std::string& error) override;
  bool stopScan(std::string& error) override;
  bool emergencyStop(std::string& error) override;

 private:
  mutable std::mutex mutex_;
  McuConfig config_;
  McuStatus status_;
  bool configured_ = false;
};

}  // namespace fmcw
