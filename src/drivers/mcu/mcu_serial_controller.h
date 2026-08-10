#pragma once

#include "core/device_interfaces.h"
#include "drivers/serial/serial_transport.h"

#include <memory>
#include <mutex>

namespace fmcw {

class McuSerialController final : public IMcuController {
 public:
  explicit McuSerialController(std::shared_ptr<ISerialTransport> transport =
                                   std::make_shared<PlatformSerialTransport>());

  std::string name() const override;
  McuStatus status() const override;
  bool connect(std::string& error) override;
  void disconnect() override;
  bool configure(const SystemConfig& config, std::string& error) override;
  bool uploadWaveform(const std::vector<McuWaveformFrame>& frames, std::string& error,
                      const McuUploadProgressCallback& progress = {}) override;
  McuWaveformSnapshotPtr loadedWaveform() const override;
  bool startScan(std::string& error) override;
  bool stopScan(std::string& error) override;
  bool emergencyStop(std::string& error) override;

 private:
  bool sendAndExpect(const std::string& command, std::string_view expected_code,
                     std::chrono::milliseconds timeout, std::string& error);

  std::shared_ptr<ISerialTransport> transport_;
  mutable std::mutex mutex_;
  McuConfig config_;
  SystemConfig system_config_;
  McuStatus status_;
  McuWaveformSnapshotPtr loaded_waveform_;
  double waveform_sample_rate_hz_ = 0.0;
  bool configured_ = false;
};

}  // namespace fmcw
