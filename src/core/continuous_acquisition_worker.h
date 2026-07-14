#pragma once

#include "core/acquisition_session.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace fmcw {

struct ContinuousAcquisitionStatus {
  bool running = false;
  bool stop_requested = false;
  bool failed = false;
  std::uint64_t batches_delivered = 0;
  std::uint64_t records_delivered = 0;
  std::uint64_t last_batch_sequence = 0;
  std::string stop_reason;
};

using RawBatchHandler = std::function<bool(RawFrameBatchPtr, std::string&)>;
using AcquisitionExitHandler = std::function<void(bool, std::string)>;

class ContinuousAcquisitionWorker {
 public:
  explicit ContinuousAcquisitionWorker(AcquisitionSession& session);
  ~ContinuousAcquisitionWorker();

  ContinuousAcquisitionWorker(const ContinuousAcquisitionWorker&) = delete;
  ContinuousAcquisitionWorker& operator=(const ContinuousAcquisitionWorker&) = delete;

  bool start(RawBatchHandler batch_handler, AcquisitionExitHandler exit_handler,
             std::string& error);
  void requestStop();
  bool waitUntilStopped(std::string& error);
  ContinuousAcquisitionStatus status() const;

 private:
  void workerLoop();

  AcquisitionSession& session_;
  mutable std::mutex mutex_;
  std::thread worker_;
  RawBatchHandler batch_handler_;
  AcquisitionExitHandler exit_handler_;
  ContinuousAcquisitionStatus status_;
  std::atomic_bool stop_requested_{false};
};

}  // namespace fmcw
