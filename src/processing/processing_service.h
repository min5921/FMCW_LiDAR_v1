#pragma once

#include "core/config_types.h"
#include "core/frame_types.h"
#include "processing/fft_backend.h"
#include "processing/processing_snapshots.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace fmcw {

enum class ProcessingEnqueueResult {
  Accepted,
  Stopping,
  Overflow,
  Error,
};

enum class ProcessingStopMode {
  DrainPending,
  DiscardPending,
};

struct ProcessingLatencyBreakdown {
  double end_to_end_ms = 0.0;
  double ownership_ms = 0.0;
  double signal_ms = 0.0;
  double acquisition_wakeup_ms = 0.0;
  double digitizer_materialization_ms = 0.0;
  double session_validation_ms = 0.0;
  double enqueue_dispatch_ms = 0.0;
  double queue_wait_ms = 0.0;
  double compute_ms = 0.0;
};

struct ProcessingServiceStatus {
  bool configured = false;
  bool running = false;
  bool stop_requested = false;
  std::size_t queue_size = 0;
  std::size_t queue_capacity = 0;
  std::size_t queue_high_water_mark = 0;
  std::uint64_t batches_discarded_on_stop = 0;
  std::uint64_t batches_processed = 0;
  std::uint64_t frames_processed = 0;
  std::uint64_t last_processed_frame_id = 0;
  std::uint64_t processing_config_revision = 0;
  double average_latency_ms = 0.0;
  double average_batch_latency_ms = 0.0;
  double last_batch_latency_ms = 0.0;
  double last_ownership_copy_latency_ms = 0.0;
  double last_signal_processing_latency_ms = 0.0;
  double maximum_ownership_copy_latency_ms = 0.0;
  double maximum_signal_processing_latency_ms = 0.0;
  double batch_latency_p50_ms = 0.0;
  double batch_latency_p95_ms = 0.0;
  double batch_latency_p99_ms = 0.0;
  double maximum_batch_latency_ms = 0.0;
  double batch_deadline_ms = 5.0;
  std::uint64_t batch_deadline_misses = 0;
  ProcessingLatencyBreakdown last_latency;
  ProcessingLatencyBreakdown average_latency;
  ProcessingLatencyBreakdown maximum_latency;
  std::string backend_name;
  std::string stop_reason;
};

using ProcessedFrameCallback = std::function<void(ProcessedFramePtr)>;

class ProcessingService {
 public:
  explicit ProcessingService(std::unique_ptr<IFftBackend> fft_backend);
  ~ProcessingService();

  ProcessingService(const ProcessingService&) = delete;
  ProcessingService& operator=(const ProcessingService&) = delete;

  bool configure(const SystemConfig& config, std::uint64_t processing_config_revision, std::string& error);
  bool start(std::string& error);
  ProcessingEnqueueResult enqueueBatch(RawFrameBatchPtr batch, std::string& error);
  ProcessingEnqueueResult enqueue(RawFramePtr frame, std::string& error);
  bool updateRuntimeConfig(const ProcessingConfig& config, std::uint64_t processing_config_revision,
                           std::string& error);
  void setProcessedFrameCallback(ProcessedFrameCallback callback);
  void requestStop(std::string reason,
                   ProcessingStopMode mode = ProcessingStopMode::DrainPending);
  bool waitUntilStopped(std::string& error);
  bool waitForProcessedBatches(std::uint64_t target_count,
                               std::chrono::milliseconds timeout,
                               std::string& error);
  bool stopRequested(std::string& reason) const;
  ProcessingServiceStatus status() const;

  ProcessingSnapshotStore& snapshots();
  const ProcessingSnapshotStore& snapshots() const;
  void setSelectedRecordIndex(std::uint32_t record_index);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fmcw
