#pragma once

#include "core/device_interfaces.h"

#include <mutex>

namespace fmcw {

class FakeEdfaController final : public IEdfaController {
 public:
  std::string name() const override;
  EdfaStatus status() const override;
  bool configure(const SystemConfig& config, std::string& error) override;
  bool connect(std::string& error) override;
  void disconnect() override;
  bool pollStatus(std::string& error) override;
  bool setControlMode(EdfaControlMode mode, std::string& error) override;
  bool setOutputSetpoint(const OpticalPowerSetpoint& setpoint, std::string& error) override;
  bool setOutputEnabled(bool enabled, std::string& error) override;
  bool resetAlarm(std::string& error) override;
  bool emergencyOff(std::string& error) override;

 private:
  mutable std::mutex mutex_;
  EdfaConfig config_;
  EdfaStatus status_;
  bool configured_ = false;
};

}  // namespace fmcw
