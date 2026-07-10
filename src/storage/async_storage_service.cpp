#include "storage/async_storage_service.h"

#include "storage/binary_storage.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <variant>

namespace fmcw {
namespace {

std::uint64_t utcNowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

}  // namespace

struct AsyncStorageService::Impl {
  using QueueItem = std::variant<RawFramePtr, ProcessedFramePtr>;

  Impl(std::unique_ptr<IRawFrameWriter> raw, std::unique_ptr<IProcessedFrameWriter> processed)
      : raw_writer(std::move(raw)), processed_writer(std::move(processed)) {}

  void workerLoop() {
    while (true) {
      QueueItem item;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return !queue.empty() || !accepting; });
        if (queue.empty() && !accepting) {
          break;
        }
        item = std::move(queue.front());
        queue.pop_front();
      }

      std::string error;
      bool written = true;
      if (std::holds_alternative<RawFramePtr>(item)) {
        const auto& frame = std::get<RawFramePtr>(item);
        written = frame != nullptr && raw_writer->write(*frame, error);
      } else {
        const auto& frame = std::get<ProcessedFramePtr>(item);
        written = frame != nullptr && processed_writer->write(*frame, error);
      }
      if (!written) {
        std::lock_guard<std::mutex> lock(mutex);
        worker_error = error.empty() ? "Storage writer rejected a frame" : error;
        stop_reason = "Storage writer failure";
        stop_requested = true;
        accepting = false;
        queue.clear();
        break;
      }
    }

    WriterFinalizeOptions finalize;
    {
      std::lock_guard<std::mutex> lock(mutex);
      finalize.end_timestamp_utc_ns = utcNowNs();
      finalize.stop_reason = stop_reason;
      finalize.completed = worker_error.empty();
    }
    std::string finalize_error;
    if (options.raw_enabled && !raw_writer->finalize(finalize, finalize_error)) {
      std::lock_guard<std::mutex> lock(mutex);
      if (worker_error.empty()) {
        worker_error = finalize_error;
      }
      finalize.completed = false;
      if (finalize.stop_reason.empty()) {
        finalize.stop_reason = "Raw writer finalization failed";
      }
    }
    finalize_error.clear();
    if (options.processed_enabled && !processed_writer->finalize(finalize, finalize_error)) {
      std::lock_guard<std::mutex> lock(mutex);
      if (worker_error.empty()) {
        worker_error = finalize_error;
      }
    }
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
  }

  EnqueueResult enqueue(QueueItem item, std::uint64_t frame_id, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!running || !accepting) {
      error = stop_reason.empty() ? "Storage service is stopping" : stop_reason;
      return EnqueueResult::Stopping;
    }
    if (queue.size() >= options.queue_capacity) {
      stop_requested = true;
      stop_reason = "Storage queue capacity exceeded";
      accepting = false;
      condition.notify_all();
      error = stop_reason;
      return EnqueueResult::Overflow;
    }
    queue.push_back(std::move(item));
    queue_high_water_mark = std::max(queue_high_water_mark, queue.size());
    last_accepted_frame_id = frame_id;
    condition.notify_one();
    error.clear();
    return EnqueueResult::Accepted;
  }

  mutable std::mutex mutex;
  std::condition_variable condition;
  std::deque<QueueItem> queue;
  std::thread worker;
  std::unique_ptr<IRawFrameWriter> raw_writer;
  std::unique_ptr<IProcessedFrameWriter> processed_writer;
  WriterOpenOptions options;
  std::size_t queue_high_water_mark = 0U;
  std::uint64_t last_accepted_frame_id = 0U;
  bool running = false;
  bool accepting = false;
  bool stop_requested = false;
  std::string stop_reason;
  std::string worker_error;
};

AsyncStorageService::AsyncStorageService()
    : AsyncStorageService(std::make_unique<BinaryRawFrameWriter>(),
                          std::make_unique<BinaryProcessedFrameWriter>()) {}

AsyncStorageService::AsyncStorageService(std::unique_ptr<IRawFrameWriter> raw_writer,
                                         std::unique_ptr<IProcessedFrameWriter> processed_writer)
    : impl_(std::make_unique<Impl>(std::move(raw_writer), std::move(processed_writer))) {}

AsyncStorageService::~AsyncStorageService() {
  requestStop("Storage service destroyed");
  std::string ignored;
  waitUntilStopped(ignored);
}

bool AsyncStorageService::start(const WriterOpenOptions& options, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->raw_writer == nullptr || impl_->processed_writer == nullptr || impl_->running ||
      impl_->worker.joinable() || (!options.raw_enabled && !options.processed_enabled) ||
      options.queue_capacity == 0U) {
    error = "Storage service options are invalid or the service is already active";
    return false;
  }
  if (options.raw_enabled && !impl_->raw_writer->open(options, error)) {
    return false;
  }
  if (options.processed_enabled && !impl_->processed_writer->open(options, error)) {
    if (options.raw_enabled) {
      WriterFinalizeOptions finalize{utcNowNs(), "Processed writer failed to open", false};
      std::string ignored;
      impl_->raw_writer->finalize(finalize, ignored);
    }
    return false;
  }
  impl_->options = options;
  impl_->queue.clear();
  impl_->queue_high_water_mark = 0U;
  impl_->last_accepted_frame_id = 0U;
  impl_->stop_requested = false;
  impl_->stop_reason.clear();
  impl_->worker_error.clear();
  impl_->accepting = true;
  impl_->running = true;
  impl_->worker = std::thread([this] { impl_->workerLoop(); });
  error.clear();
  return true;
}

EnqueueResult AsyncStorageService::enqueueRaw(RawFramePtr frame, std::string& error) {
  if (!frame) {
    error = "Cannot enqueue a null raw frame";
    return EnqueueResult::Error;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->options.raw_enabled) {
      error.clear();
      return EnqueueResult::Accepted;
    }
  }
  const auto frame_id = frame->metadata.frame_id;
  return impl_->enqueue(std::move(frame), frame_id, error);
}

EnqueueResult AsyncStorageService::enqueueProcessed(ProcessedFramePtr frame, std::string& error) {
  if (!frame) {
    error = "Cannot enqueue a null processed frame";
    return EnqueueResult::Error;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->options.processed_enabled) {
      error.clear();
      return EnqueueResult::Accepted;
    }
  }
  const auto frame_id = frame->frame_id;
  return impl_->enqueue(std::move(frame), frame_id, error);
}

void AsyncStorageService::requestStop(std::string reason) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->running && !impl_->worker.joinable()) {
    return;
  }
  impl_->accepting = false;
  impl_->stop_requested = true;
  if (impl_->stop_reason.empty()) {
    impl_->stop_reason = std::move(reason);
  }
  impl_->condition.notify_all();
}

bool AsyncStorageService::waitUntilStopped(std::string& error) {
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->worker_error.empty()) {
    error = impl_->worker_error;
    return false;
  }
  error.clear();
  return true;
}

StorageStatus AsyncStorageService::status() const {
  StorageStatus status;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    status.queue_size = impl_->queue.size();
    status.queue_capacity = impl_->options.queue_capacity;
    status.queue_high_water_mark = impl_->queue_high_water_mark;
    status.overflow_policy = impl_->options.overflow_policy;
    status.stop_requested = impl_->stop_requested;
    status.last_accepted_frame_id = impl_->last_accepted_frame_id;
    status.stop_reason = impl_->stop_reason;
  }
  status.raw_writer = impl_->raw_writer->status();
  status.processed_writer = impl_->processed_writer->status();
  return status;
}

}  // namespace fmcw
