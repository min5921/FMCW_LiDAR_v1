#pragma once

#include "core/config_types.h"
#include "core/frame_types.h"
#include "processing/fft_backend.h"
#include "processing/processing_snapshots.h"

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

struct ProcessingServiceStatus {
  bool configured = false;
  bool running = false;
  bool stop_requested = false;
  std::size_t queue_size = 0;
  std::size_t queue_capacity = 0;
  std::size_t queue_high_water_mark = 0;
  std::uint64_t frames_processed = 0;
  std::uint64_t last_processed_frame_id = 0;
  std::uint64_t processing_config_revision = 0;
  double average_latency_ms = 0.0;
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
  ProcessingEnqueueResult enqueue(RawFramePtr frame, std::string& error);
  bool updateRuntimeConfig(const ProcessingConfig& config, std::uint64_t processing_config_revision,
                           std::string& error);
  void setProcessedFrameCallback(ProcessedFrameCallback callback);
  void requestStop(std::string reason);
  bool waitUntilStopped(std::string& error);
  ProcessingServiceStatus status() const;

  ProcessingSnapshotStore& snapshots();
  const ProcessingSnapshotStore& snapshots() const;
  void setSelectedRecordIndex(std::uint32_t record_index);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fmcw
