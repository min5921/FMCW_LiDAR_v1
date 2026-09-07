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
  using RawQueueItem = std::variant<RawFramePtr, RawFrameBatchPtr, ProcessingConfigEvent>;

  Impl(std::unique_ptr<IRawFrameWriter> raw,
       std::unique_ptr<IPointCloudFrameWriter> point_cloud)
      : raw_writer(std::move(raw)), point_cloud_writer(std::move(point_cloud)) {}

  WriterFinalizeOptions finalizeOptions() const {
    std::lock_guard<std::mutex> lock(mutex);
    return {utcNowNs(), worker_error.empty() ? stop_reason : worker_error,
            !failed && worker_error.empty()};
  }

  void failWriter(std::string error, std::string reason) {
    std::lock_guard<std::mutex> lock(mutex);
    if (worker_error.empty()) {
      worker_error = error.empty() ? "Storage writer rejected queued data" : std::move(error);
    }
    if (stop_reason.empty()) {
      stop_reason = std::move(reason);
    }
    stop_requested = true;
    failed = true;
    accepting = false;
    raw_queue.clear();
    point_cloud_queue.clear();
    raw_condition.notify_all();
    processed_condition.notify_all();
  }

  void finishWorker(bool raw) {
    std::lock_guard<std::mutex> lock(mutex);
    if (raw) {
      raw_running = false;
    } else {
      processed_running = false;
    }
    running = raw_running || processed_running;
  }

  void rawWorkerLoop() {
    while (true) {
      RawQueueItem item;
      {
        std::unique_lock<std::mutex> lock(mutex);
        raw_condition.wait(lock, [this] { return !raw_queue.empty() || !accepting; });
        if (raw_queue.empty() && !accepting) {
          break;
        }
        item = std::move(raw_queue.front());
        raw_queue.pop_front();
      }

      std::string error;
      bool written = false;
      if (const auto* event = std::get_if<ProcessingConfigEvent>(&item)) {
        written = writeProcessingEvent(options.session_directory, options.file_stem, *event, error);
      } else if (std::holds_alternative<RawFrameBatchPtr>(item)) {
        const auto& batch = std::get<RawFrameBatchPtr>(item);
        written = batch != nullptr && raw_writer->writeBatch(*batch, error);
      } else {
        const auto& frame = std::get<RawFramePtr>(item);
        written = frame != nullptr && raw_writer->write(*frame, error);
      }
      if (!written) {
        failWriter(std::move(error), "Raw storage writer failure");
        break;
      }
    }

    finishWorker(true);
  }

  void pointCloudWorkerLoop() {
    while (true) {
      std::shared_ptr<const PointCloudSnapshot> frame;
      {
        std::unique_lock<std::mutex> lock(mutex);
        processed_condition.wait(lock, [this] { return !point_cloud_queue.empty() || !accepting; });
        if (point_cloud_queue.empty() && !accepting) {
          break;
        }
        frame = std::move(point_cloud_queue.front());
        point_cloud_queue.pop_front();
      }

      std::string error;
      if (!frame || !point_cloud_writer->write(*frame, error)) {
        failWriter(std::move(error), "Point cloud storage writer failure");
        break;
      }
    }

    finishWorker(false);
  }

  EnqueueResult enqueueRawItem(RawQueueItem item, std::uint64_t frame_id,
                               std::uint64_t block_sequence, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!options.raw_enabled) {
      error.clear();
      return EnqueueResult::Accepted;
    }
    if (!running || !accepting) {
      error = stop_reason.empty() ? "Storage service is stopping" : stop_reason;
      return EnqueueResult::Stopping;
    }
    if (raw_queue.size() >= options.queue_capacity) {
      stop_requested = true;
      failed = true;
      stop_reason = "Raw storage queue capacity exceeded";
      accepting = false;
      raw_condition.notify_all();
      processed_condition.notify_all();
      error = stop_reason;
      return EnqueueResult::Overflow;
    }
    const bool is_data = !std::holds_alternative<ProcessingConfigEvent>(item);
    raw_queue.push_back(std::move(item));
    raw_queue_high_water_mark = std::max(raw_queue_high_water_mark, raw_queue.size());
    if (is_data) {
      last_accepted_frame_id = frame_id;
      last_accepted_raw_block = block_sequence;
    }
    raw_condition.notify_one();
    error.clear();
    return EnqueueResult::Accepted;
  }

  EnqueueResult enqueuePointCloudItem(std::shared_ptr<const PointCloudSnapshot> frame,
                                      std::string& error) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!options.processed_enabled) {
      error.clear();
      return EnqueueResult::Accepted;
    }
    if (!running || !accepting) {
      error = stop_reason.empty() ? "Storage service is stopping" : stop_reason;
      return EnqueueResult::Stopping;
    }
    if (point_cloud_queue.size() >= options.queue_capacity) {
      stop_requested = true;
      failed = true;
      stop_reason = "Point cloud storage queue capacity exceeded";
      accepting = false;
      raw_condition.notify_all();
      processed_condition.notify_all();
      error = stop_reason;
      return EnqueueResult::Overflow;
    }
    last_accepted_frame_id = frame->last_frame_id;
    point_cloud_queue.push_back(std::move(frame));
    processed_queue_high_water_mark = std::max(
        processed_queue_high_water_mark, point_cloud_queue.size());
    processed_condition.notify_one();
    error.clear();
    return EnqueueResult::Accepted;
  }

  mutable std::mutex mutex;
  std::condition_variable raw_condition;
  std::condition_variable processed_condition;
  std::deque<RawQueueItem> raw_queue;
  std::deque<std::shared_ptr<const PointCloudSnapshot>> point_cloud_queue;
  std::thread raw_worker;
  std::thread processed_worker;
  std::unique_ptr<IRawFrameWriter> raw_writer;
  std::unique_ptr<IPointCloudFrameWriter> point_cloud_writer;
  WriterOpenOptions options;
  std::size_t raw_queue_high_water_mark = 0U;
  std::size_t processed_queue_high_water_mark = 0U;
  std::uint64_t last_accepted_frame_id = 0U;
  std::uint64_t last_accepted_raw_block = 0U;
  bool running = false;
  bool raw_running = false;
  bool processed_running = false;
  bool accepting = false;
  bool stop_requested = false;
  bool failed = false;
  bool needs_finalization = false;
  std::string stop_reason;
  std::string worker_error;
};

AsyncStorageService::AsyncStorageService()
    : AsyncStorageService(std::make_unique<BinaryRawFrameWriter>(),
                          std::make_unique<BinaryPointCloudFrameWriter>()) {}

AsyncStorageService::AsyncStorageService(std::unique_ptr<IRawFrameWriter> raw_writer,
                                         std::unique_ptr<IPointCloudFrameWriter> point_cloud_writer)
    : impl_(std::make_unique<Impl>(std::move(raw_writer), std::move(point_cloud_writer))) {}

AsyncStorageService::~AsyncStorageService() {
  requestStop("Storage service destroyed");
  std::string ignored;
  waitUntilStopped(ignored);
}

EnqueueResult AsyncStorageService::enqueueProcessingEvent(ProcessingConfigEvent event, std::string& error) {
  const auto frame_id = event.first_source_frame_id;
  return impl_->enqueueRawItem(std::move(event), frame_id, 0U, error);
}

bool AsyncStorageService::start(const WriterOpenOptions& options, std::string& error) {
  std::unique_lock<std::mutex> lock(impl_->mutex);
  if (impl_->raw_writer == nullptr || impl_->point_cloud_writer == nullptr || impl_->running ||
      impl_->raw_worker.joinable() || impl_->processed_worker.joinable() ||
      (!options.raw_enabled && !options.processed_enabled) || options.queue_capacity == 0U) {
    error = "Storage service options are invalid or the service is already active";
    return false;
  }
  if (options.raw_enabled && !impl_->raw_writer->open(options, error)) {
    return false;
  }
  if (options.processed_enabled && !impl_->point_cloud_writer->open(options, error)) {
    if (options.raw_enabled) {
      WriterFinalizeOptions finalize{utcNowNs(), "Point cloud writer failed to open", false};
      std::string ignored;
      impl_->raw_writer->finalize(finalize, ignored);
    }
    return false;
  }

  impl_->options = options;
  impl_->raw_queue.clear();
  impl_->point_cloud_queue.clear();
  impl_->raw_queue_high_water_mark = 0U;
  impl_->processed_queue_high_water_mark = 0U;
  impl_->last_accepted_frame_id = 0U;
  impl_->last_accepted_raw_block = 0U;
  impl_->stop_requested = false;
  impl_->failed = false;
  impl_->stop_reason.clear();
  impl_->worker_error.clear();
  impl_->accepting = true;
  impl_->raw_running = options.raw_enabled;
  impl_->processed_running = options.processed_enabled;
  impl_->running = true;
  impl_->needs_finalization = true;
  try {
    if (options.raw_enabled) {
      impl_->raw_worker = std::thread([this] {
        try { impl_->rawWorkerLoop(); }
        catch (const std::exception& e) { impl_->failWriter(e.what(), "Raw writer exception"); impl_->finishWorker(true); }
        catch (...) { impl_->failWriter("Unknown raw writer exception", "Raw writer exception"); impl_->finishWorker(true); }
      });
    }
    if (options.processed_enabled) {
      impl_->processed_worker = std::thread([this] {
        try { impl_->pointCloudWorkerLoop(); }
        catch (const std::exception& e) { impl_->failWriter(e.what(), "Point-cloud writer exception"); impl_->finishWorker(false); }
        catch (...) { impl_->failWriter("Unknown point-cloud writer exception", "Point-cloud writer exception"); impl_->finishWorker(false); }
      });
    }
  } catch (const std::exception& e) {
    const std::string failure = std::string("Cannot start storage worker: ") + e.what();
    lock.unlock();
    impl_->failWriter(failure, failure);
    waitUntilStopped(error);
    return false;
  }
  error.clear();
  return true;
}

EnqueueResult AsyncStorageService::enqueueRawBatch(RawFrameBatchPtr batch, std::string& error) {
  if (!batch || batch->records.empty()) {
    error = "Cannot enqueue a null or empty raw DMA block";
    return EnqueueResult::Error;
  }
  const auto frame_id = batch->records.back().metadata.frame_id;
  const auto sequence = batch->metadata.sequence;
  return impl_->enqueueRawItem(std::move(batch), frame_id, sequence, error);
}

EnqueueResult AsyncStorageService::enqueueRaw(RawFramePtr frame, std::string& error) {
  if (!frame) {
    error = "Cannot enqueue a null raw frame";
    return EnqueueResult::Error;
  }
  const auto frame_id = frame->metadata.frame_id;
  const auto sequence = frame->metadata.dma_buffer_sequence;
  return impl_->enqueueRawItem(std::move(frame), frame_id, sequence, error);
}

EnqueueResult AsyncStorageService::enqueuePointCloud(
    std::shared_ptr<const PointCloudSnapshot> frame, std::string& error) {
  if (!frame || !frame->complete) {
    error = "Cannot enqueue a null or incomplete point cloud frame";
    return EnqueueResult::Error;
  }
  return impl_->enqueuePointCloudItem(std::move(frame), error);
}

void AsyncStorageService::requestStop(std::string reason, bool failed) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->running && !impl_->raw_worker.joinable() && !impl_->processed_worker.joinable()) {
    return;
  }
  impl_->accepting = false;
  impl_->stop_requested = true;
  impl_->failed = impl_->failed || failed;
  if (failed && impl_->worker_error.empty()) {
    impl_->worker_error = reason;
  }
  if (impl_->stop_reason.empty()) {
    impl_->stop_reason = std::move(reason);
  }
  impl_->raw_condition.notify_all();
  impl_->processed_condition.notify_all();
}

bool AsyncStorageService::waitUntilStopped(std::string& error) {
  if (impl_->raw_worker.joinable()) {
    impl_->raw_worker.join();
  }
  if (impl_->processed_worker.joinable()) {
    impl_->processed_worker.join();
  }
  if (impl_->needs_finalization) {
    // Both writers must finish consuming before either stream is marked complete.
    const auto finalize = [this](auto& writer, bool enabled, const char* reason) {
      if (enabled) {
        std::string writer_error;
        try {
          if (!writer->finalize(impl_->finalizeOptions(), writer_error)) {
            impl_->failWriter(std::move(writer_error), reason);
          }
        } catch (const std::exception& e) {
          impl_->failWriter(e.what(), reason);
        } catch (...) {
          impl_->failWriter("Unknown storage finalization exception", reason);
        }
      }
    };
    finalize(impl_->raw_writer, impl_->options.raw_enabled, "Raw writer finalization failed");
    finalize(impl_->point_cloud_writer, impl_->options.processed_enabled,
             "Point cloud writer finalization failed");
    if (!impl_->finalizeOptions().completed) {
      // A later finalizer failure also invalidates an already closed peer stream.
      finalize(impl_->raw_writer, impl_->options.raw_enabled, "Raw metadata finalization failed");
      finalize(impl_->point_cloud_writer, impl_->options.processed_enabled,
               "Point cloud metadata finalization failed");
    }
    impl_->needs_finalization = false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->raw_running = impl_->processed_running = impl_->running = false;
  if (impl_->failed || !impl_->worker_error.empty()) {
    error = impl_->worker_error.empty() ? impl_->stop_reason : impl_->worker_error;
    return false;
  }
  error.clear();
  return true;
}

StorageStatus AsyncStorageService::status() const {
  StorageStatus status;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    status.raw_queue_size = impl_->raw_queue.size();
    status.raw_queue_capacity = impl_->options.raw_enabled ? impl_->options.queue_capacity : 0U;
    status.raw_queue_high_water_mark = impl_->raw_queue_high_water_mark;
    status.processed_queue_size = impl_->point_cloud_queue.size();
    status.processed_queue_capacity = impl_->options.processed_enabled ? impl_->options.queue_capacity : 0U;
    status.processed_queue_high_water_mark = impl_->processed_queue_high_water_mark;
    status.queue_size = std::max(status.raw_queue_size, status.processed_queue_size);
    status.queue_capacity = impl_->options.queue_capacity;
    status.queue_high_water_mark = std::max(
        status.raw_queue_high_water_mark, status.processed_queue_high_water_mark);
    status.overflow_policy = impl_->options.overflow_policy;
    status.stop_requested = impl_->stop_requested;
    status.failed = impl_->failed;
    status.last_accepted_frame_id = impl_->last_accepted_frame_id;
    status.last_accepted_raw_block = impl_->last_accepted_raw_block;
    status.stop_reason = impl_->stop_reason;
  }
  status.raw_writer = impl_->raw_writer->status();
  status.processed_writer = impl_->point_cloud_writer->status();
  return status;
}

}  // namespace fmcw
