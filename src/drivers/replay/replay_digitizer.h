#pragma once

#include "core/device_interfaces.h"
#include "core/raw_frame_batch_pool.h"
#include "storage/binary_storage.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace fmcw {

class ReplayDigitizer final : public IDigitizer {
 public:
  std::string name() const override;
  DigitizerTelemetry telemetry() const override;
  bool connect(std::string& error) override;
  void disconnect() override;
  bool configure(const SystemConfig& config, std::string& error) override;
  bool start(std::string& error) override;
  FrameWaitResult waitForBatch(MutableRawFrameBatchPtr& batch,
                               std::chrono::milliseconds timeout,
                               std::string& error) override;
  FrameWaitResult waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout,
                               std::string& error) override;
  bool abort(std::string& error) override;
  bool stop(std::string& error) override;

 private:
  bool openReader(std::string& error);

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  RawFrameBatchPool batch_pool_;
  RawReplayReader reader_;
  SystemConfig config_;
  DigitizerTelemetry telemetry_;
  MutableRawFrameBatchPtr compatibility_batch_;
  std::size_t compatibility_record_index_ = 0;
  std::chrono::steady_clock::time_point next_batch_due_;
  std::uint64_t next_frame_id_ = 1;
  bool configured_ = false;
  bool aborted_ = false;
  bool end_pending_ = false;
};

}  // namespace fmcw
