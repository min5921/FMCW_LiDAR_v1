#pragma once

#include "core/devices/device_interfaces.h"
#include "drivers/edfa/edfa_protocol.h"
#include "drivers/serial/serial_transport.h"

#include <memory>
#include <mutex>

namespace fmcw {

class EdfaSerialController final : public IEdfaController {
 public:
  explicit EdfaSerialController(std::shared_ptr<ISerialTransport> transport =
                                    std::make_shared<PlatformSerialTransport>());

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
  void publishStatus(EdfaStatus next);
  // Callers hold io_mutex_; status_mutex_ is never held during serial I/O.
  bool setOutputEnabledLocked(bool enabled, std::string& error);
  bool transact(const std::vector<std::uint8_t>& command, EdfaPacket& response, std::string& error);
  bool refreshReading(EdfaStatus& next, std::string& error);
  bool refreshDeviceState(EdfaStatus& next, std::string& error);

  std::shared_ptr<ISerialTransport> transport_;
  std::mutex io_mutex_;
  mutable std::mutex status_mutex_;
  EdfaConfig config_;
  EdfaStatus status_;
  bool configured_ = false;
};

}  // namespace fmcw
