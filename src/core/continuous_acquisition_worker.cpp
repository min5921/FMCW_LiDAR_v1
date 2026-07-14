#include "core/continuous_acquisition_worker.h"

#include <chrono>
#include <utility>

namespace fmcw {

ContinuousAcquisitionWorker::ContinuousAcquisitionWorker(AcquisitionSession& session)
    : session_(session) {}

ContinuousAcquisitionWorker::~ContinuousAcquisitionWorker() {
  requestStop();
  std::string ignored;
  waitUntilStopped(ignored);
}

bool ContinuousAcquisitionWorker::start(RawBatchHandler batch_handler,
                                        AcquisitionExitHandler exit_handler,
                                        std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!session_.running() || worker_.joinable() || status_.running || !batch_handler) {
    error = "Continuous acquisition worker requires a running session and a batch handler";
    return false;
  }
  batch_handler_ = std::move(batch_handler);
  exit_handler_ = std::move(exit_handler);
  status_ = {};
  status_.running = true;
  stop_requested_.store(false);
  worker_ = std::thread([this] { workerLoop(); });
  error.clear();
  return true;
}

void ContinuousAcquisitionWorker::requestStop() {
  stop_requested_.store(true);
  std::lock_guard<std::mutex> lock(mutex_);
  status_.stop_requested = true;
}

bool ContinuousAcquisitionWorker::waitUntilStopped(std::string& error) {
  if (worker_.joinable()) {
    worker_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.failed) {
    error = status_.stop_reason;
    return false;
  }
  error.clear();
  return true;
}

ContinuousAcquisitionStatus ContinuousAcquisitionWorker::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

void ContinuousAcquisitionWorker::workerLoop() {
  bool failed = false;
  std::string reason;
  while (!stop_requested_.load()) {
    RawFrameBatchPtr batch;
    std::string error;
    const auto result = session_.waitForBatch(batch, std::chrono::milliseconds(50), error);
    if (result == FrameWaitResult::Timeout) {
      continue;
    }
    if (result == FrameWaitResult::Stopped) {
      reason = "Acquisition source completed";
      break;
    }
    if (result == FrameWaitResult::Error) {
      failed = true;
      reason = error.empty() ? "Acquisition source failed" : std::move(error);
      break;
    }
    if (!batch_handler_(batch, error)) {
      failed = true;
      reason = error.empty() ? "DMA batch consumer requested Stop" : std::move(error);
      break;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++status_.batches_delivered;
      status_.records_delivered += batch->records.size();
      status_.last_batch_sequence = batch->metadata.sequence;
    }
  }

  AcquisitionExitHandler exit_handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.running = false;
    status_.stop_requested = stop_requested_.load();
    status_.failed = failed;
    status_.stop_reason = reason;
    exit_handler = exit_handler_;
  }
  if (!stop_requested_.load() && exit_handler) {
    exit_handler(failed, std::move(reason));
  }
}

}  // namespace fmcw
