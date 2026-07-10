#pragma once

#include "core/device_interfaces.h"

#include <cstdint>
#include <mutex>

namespace fmcw {

class FakeDigitizer final : public IDigitizer {
 public:
  std::string name() const override;
  DigitizerTelemetry telemetry() const override;
  bool connect(std::string& error) override;
  void disconnect() override;
  bool configure(const SystemConfig& config, std::string& error) override;
  bool start(std::string& error) override;
  FrameWaitResult waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout, std::string& error) override;
  bool abort(std::string& error) override;
  bool stop(std::string& error) override;

 private:
  void fillFrame(RawFrame& frame, std::uint64_t frame_id) const;

  mutable std::mutex mutex_;
  SystemConfig config_;
  DigitizerTelemetry telemetry_;
  bool configured_ = false;
  bool aborted_ = false;
  std::uint64_t next_frame_id_ = 1;
};

}  // namespace fmcw
