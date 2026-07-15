#pragma once

#include "core/device_interfaces.h"
#include "core/raw_frame_batch_pool.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace fmcw {

class FakeDigitizer final : public IDigitizer {
 public:
  ~FakeDigitizer() override;

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
  struct CompletedDmaBuffer {
    std::uint64_t sequence = 0;
    std::uint64_t completion_timestamp_ns = 0;
    std::uint32_t record_count = 0;
  };

  void producerLoop();
  void stopProducer();
  void buildSignalTemplates();
  void fillFrame(RawFrame& frame, std::uint64_t frame_id, std::uint64_t batch_sequence,
                 std::uint32_t record_index, std::uint32_t records_in_batch,
                 std::uint64_t completion_timestamp_ns) const;

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  RawFrameBatchPool batch_pool_;
  SystemConfig config_;
  DigitizerTelemetry telemetry_;
  std::vector<std::vector<std::int16_t>> signal_templates_;
  std::deque<CompletedDmaBuffer> completed_dma_buffers_;
  std::thread producer_thread_;
  MutableRawFrameBatchPtr compatibility_batch_;
  std::size_t compatibility_record_index_ = 0;
  std::chrono::steady_clock::time_point next_batch_due_;
  std::chrono::steady_clock::duration batch_period_{};
  bool configured_ = false;
  bool aborted_ = false;
  bool producer_stop_requested_ = false;
  bool dma_overflow_latched_ = false;
  bool source_exhausted_ = false;
  std::uint64_t next_dma_sequence_ = 0;
  std::uint64_t scheduled_frame_count_ = 0;
  std::uint64_t next_frame_id_ = 1;
};

}  // namespace fmcw
