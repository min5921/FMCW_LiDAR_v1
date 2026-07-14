#include "processing/processing_service.h"

#include "core/config_validation.h"
#include "processing/signal_processor.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace fmcw {

struct ProcessingService::Impl {
  explicit Impl(std::unique_ptr<IFftBackend> backend) : processor(std::move(backend)) {}

  struct PendingRuntimeConfig {
    ProcessingConfig config;
    std::uint64_t revision = 0;
  };

  void workerLoop() {
    while (true) {
      RawFrameBatchPtr batch;
      std::optional<PendingRuntimeConfig> runtime_update;
      ProcessedFrameCallback current_callback;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return !queue.empty() || !accepting; });
        if (queue.empty() && !accepting) {
          break;
        }
        batch = std::move(queue.front());
        queue.pop_front();
        runtime_update = std::move(pending_runtime_config);
        pending_runtime_config.reset();
        current_callback = callback;
      }
      std::string error;
      if (runtime_update.has_value() &&
          !processor.updateRuntimeConfig(runtime_update->config, runtime_update->revision, error)) {
        std::lock_guard<std::mutex> lock(mutex);
        worker_error = error;
        stop_reason = "Runtime processing configuration failed";
        stop_requested = true;
        accepting = false;
        queue.clear();
        break;
      }
      bool batch_complete = true;
      for (std::size_t record_index = 0; record_index < batch->records.size(); ++record_index) {
        const auto raw = rawFrameAt(batch, record_index);
        auto processed = std::make_shared<ProcessedFrame>();
        if (!raw || !processor.process(*raw, *processed, error)) {
          std::lock_guard<std::mutex> lock(mutex);
          worker_error = error.empty() ? "DMA batch contains an invalid raw frame" : error;
          stop_reason = "Signal processing failed";
          stop_requested = true;
          accepting = false;
          queue.clear();
          batch_complete = false;
          break;
        }
        snapshots.publish(*raw, *processed);
        {
          std::lock_guard<std::mutex> lock(mutex);
          ++frames_processed;
          last_processed_frame_id = processed->frame_id;
          latency_total_ms += processed->processing_latency_ms;
          processing_config_revision = processed->processing_config_revision;
          if (processed->stop_requested) {
            stop_requested = true;
            stop_reason = "Processing requested acquisition stop";
            accepting = false;
            queue.clear();
          }
        }
        if (current_callback) {
          current_callback(processed);
        }
        if (processed->stop_requested) {
          batch_complete = false;
          break;
        }
      }
      if (batch_complete) {
        std::lock_guard<std::mutex> lock(mutex);
        ++batches_processed;
      }
      if (!batch_complete) {
        break;
      }
    }
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
  }

  mutable std::mutex mutex;
  std::condition_variable condition;
  std::deque<RawFrameBatchPtr> queue;
  std::thread worker;
  SignalProcessor processor;
  ProcessingSnapshotStore snapshots;
  SystemConfig config;
  std::optional<PendingRuntimeConfig> pending_runtime_config;
  ProcessedFrameCallback callback;
  std::size_t queue_capacity = 0;
  std::size_t queue_high_water_mark = 0;
  std::uint64_t batches_processed = 0;
  std::uint64_t frames_processed = 0;
  std::uint64_t last_processed_frame_id = 0;
  std::uint64_t processing_config_revision = 0;
  double latency_total_ms = 0.0;
  bool configured = false;
  bool running = false;
  bool accepting = false;
  bool stop_requested = false;
  std::string stop_reason;
  std::string worker_error;
};

ProcessingService::ProcessingService(std::unique_ptr<IFftBackend> fft_backend)
    : impl_(std::make_unique<Impl>(std::move(fft_backend))) {}

ProcessingService::~ProcessingService() {
  requestStop("Processing service destroyed");
  std::string ignored;
  waitUntilStopped(ignored);
}

bool ProcessingService::configure(const SystemConfig& config, std::uint64_t processing_config_revision,
                                  std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->running) {
    error = "Cannot configure the processing service while it is running";
    return false;
  }
  if (!impl_->processor.configure(config, processing_config_revision, error)) {
    return false;
  }
  impl_->config = config;
  impl_->queue_capacity = config.processing.queue_capacity;
  impl_->processing_config_revision = processing_config_revision;
  impl_->snapshots.configure(config.scan.x_pixel_count, config.scan.y_line_count);
  impl_->configured = true;
  error.clear();
  return true;
}

bool ProcessingService::start(std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->configured || impl_->running || impl_->worker.joinable()) {
    error = "Processing service is not configured or is already active";
    return false;
  }
  impl_->queue.clear();
  impl_->queue_high_water_mark = 0;
  impl_->batches_processed = 0;
  impl_->frames_processed = 0;
  impl_->last_processed_frame_id = 0;
  impl_->latency_total_ms = 0.0;
  impl_->stop_requested = false;
  impl_->stop_reason.clear();
  impl_->worker_error.clear();
  impl_->accepting = true;
  impl_->running = true;
  impl_->worker = std::thread([this] { impl_->workerLoop(); });
  error.clear();
  return true;
}

ProcessingEnqueueResult ProcessingService::enqueueBatch(RawFrameBatchPtr batch, std::string& error) {
  if (!batch || batch->records.empty()) {
    error = "Cannot enqueue a null or empty raw DMA batch";
    return ProcessingEnqueueResult::Error;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->running || !impl_->accepting) {
    error = impl_->stop_reason.empty() ? "Processing service is stopping" : impl_->stop_reason;
    return ProcessingEnqueueResult::Stopping;
  }
  if (impl_->queue.size() >= impl_->queue_capacity) {
    impl_->stop_requested = true;
    impl_->stop_reason = "Processing queue capacity exceeded";
    impl_->accepting = false;
    impl_->condition.notify_all();
    error = impl_->stop_reason;
    return ProcessingEnqueueResult::Overflow;
  }
  impl_->queue.push_back(std::move(batch));
  impl_->queue_high_water_mark = std::max(impl_->queue_high_water_mark, impl_->queue.size());
  impl_->condition.notify_one();
  error.clear();
  return ProcessingEnqueueResult::Accepted;
}

ProcessingEnqueueResult ProcessingService::enqueue(RawFramePtr frame, std::string& error) {
  if (!frame) {
    error = "Cannot enqueue a null raw frame";
    return ProcessingEnqueueResult::Error;
  }
  auto batch = std::make_shared<RawFrameBatch>();
  batch->metadata.sequence = frame->metadata.dma_buffer_sequence;
  batch->metadata.completion_timestamp_ns = frame->metadata.host_timestamp_ns;
  batch->metadata.record_count = 1;
  batch->metadata.record_length = frame->metadata.record_length;
  batch->records.push_back(*frame);
  return enqueueBatch(std::move(batch), error);
}

bool ProcessingService::updateRuntimeConfig(const ProcessingConfig& config,
                                            std::uint64_t processing_config_revision, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->configured || !impl_->running || !impl_->accepting) {
    error = "Runtime processing settings require an active processing service";
    return false;
  }
  auto candidate = impl_->config;
  candidate.processing = config;
  if (ConfigValidator::validate(candidate).hasErrors()) {
    error = "Runtime processing settings failed validation";
    return false;
  }
  if (config.fft_backend != impl_->config.processing.fft_backend ||
      config.queue_capacity != impl_->config.processing.queue_capacity ||
      config.overflow_policy != impl_->config.processing.overflow_policy) {
    error = "FFT backend, queue capacity, and overflow policy cannot change while running";
    return false;
  }
  impl_->config.processing = config;
  impl_->pending_runtime_config = Impl::PendingRuntimeConfig{config, processing_config_revision};
  error.clear();
  return true;
}

void ProcessingService::setProcessedFrameCallback(ProcessedFrameCallback callback) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->callback = std::move(callback);
}

void ProcessingService::requestStop(std::string reason) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->running && !impl_->worker.joinable()) {
    return;
  }
  impl_->accepting = false;
  if (impl_->stop_reason.empty()) {
    impl_->stop_reason = std::move(reason);
  }
  impl_->condition.notify_all();
}

bool ProcessingService::waitUntilStopped(std::string& error) {
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

ProcessingServiceStatus ProcessingService::status() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ProcessingServiceStatus status;
  status.configured = impl_->configured;
  status.running = impl_->running;
  status.stop_requested = impl_->stop_requested;
  status.queue_size = impl_->queue.size();
  status.queue_capacity = impl_->queue_capacity;
  status.queue_high_water_mark = impl_->queue_high_water_mark;
  status.batches_processed = impl_->batches_processed;
  status.frames_processed = impl_->frames_processed;
  status.last_processed_frame_id = impl_->last_processed_frame_id;
  status.processing_config_revision = impl_->processing_config_revision;
  status.average_latency_ms = impl_->frames_processed == 0U
      ? 0.0
      : impl_->latency_total_ms / static_cast<double>(impl_->frames_processed);
  status.backend_name = impl_->processor.backendName();
  status.stop_reason = impl_->stop_reason;
  return status;
}

ProcessingSnapshotStore& ProcessingService::snapshots() { return impl_->snapshots; }

const ProcessingSnapshotStore& ProcessingService::snapshots() const { return impl_->snapshots; }

void ProcessingService::setSelectedRecordIndex(std::uint32_t record_index) {
  impl_->snapshots.setSelectedRecordIndex(record_index);
}

}  // namespace fmcw
