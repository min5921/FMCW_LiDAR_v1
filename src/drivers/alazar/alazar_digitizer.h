#pragma once

#include "core/device_interfaces.h"

#include <memory>
#include <mutex>

namespace fmcw {

class AlazarDigitizer final : public IDigitizer {
 public:
  AlazarDigitizer();
  ~AlazarDigitizer() override;

  AlazarDigitizer(const AlazarDigitizer&) = delete;
  AlazarDigitizer& operator=(const AlazarDigitizer&) = delete;

  static bool sdkAvailable();

  std::string name() const override;
  DigitizerTelemetry telemetry() const override;
  bool connect(std::string& error) override;
  void disconnect() override;
  bool configure(const SystemConfig& config, std::string& error) override;
  bool start(std::string& error) override;
  FrameWaitResult waitForBatch(MutableRawFrameBatchPtr& batch,
                               std::chrono::milliseconds timeout,
                               std::string& error) override;
  FrameWaitResult waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout, std::string& error) override;
  bool abort(std::string& error) override;
  bool stop(std::string& error) override;

 private:
  struct Impl;
  bool abortAsyncReadLocked(std::unique_lock<std::mutex>& lock, std::string& error);
  bool configureBoard(std::string& error);
  void releaseBuffers();

  mutable std::mutex mutex_;
  mutable std::mutex telemetry_mutex_;
  std::unique_ptr<Impl> impl_;
  SystemConfig config_;
  DigitizerTelemetry telemetry_;
  bool configured_ = false;
};

}  // namespace fmcw
