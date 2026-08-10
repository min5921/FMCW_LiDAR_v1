#include "processing/processing_service.h"

#include "core/config_validation.h"
#include "core/realtime_thread.h"
#include "processing/signal_processor.h"

#include <condition_variable>
#include <deque>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace fmcw {
namespace {

constexpr double kBatchDeadlineMs = 5.0;
constexpr std::size_t kLatencyWindowSize = 4096U;

std::uint64_t nowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

double elapsedMs(std::uint64_t start_ns, std::uint64_t end_ns) {
  return start_ns == 0U || end_ns < start_ns ? 0.0 :
      static_cast<double>(end_ns - start_ns) * 1.0e-6;
}

std::uint64_t orderedTimestamp(std::uint64_t timestamp_ns,
                               std::uint64_t preceding_timestamp_ns) {
  return timestamp_ns == 0U || timestamp_ns < preceding_timestamp_ns
      ? preceding_timestamp_ns
      : timestamp_ns;
}

void addLatency(ProcessingLatencyBreakdown& total,
                const ProcessingLatencyBreakdown& value) {
  total.end_to_end_ms += value.end_to_end_ms;
  total.ownership_ms += value.ownership_ms;
  total.signal_ms += value.signal_ms;
  total.acquisition_wakeup_ms += value.acquisition_wakeup_ms;
  total.digitizer_materialization_ms += value.digitizer_materialization_ms;
  total.session_validation_ms += value.session_validation_ms;
  total.enqueue_dispatch_ms += value.enqueue_dispatch_ms;
  total.queue_wait_ms += value.queue_wait_ms;
  total.compute_ms += value.compute_ms;
}

void maximizeLatency(ProcessingLatencyBreakdown& maximum,
                     const ProcessingLatencyBreakdown& value) {
  maximum.end_to_end_ms = std::max(maximum.end_to_end_ms, value.end_to_end_ms);
  maximum.ownership_ms = std::max(maximum.ownership_ms, value.ownership_ms);
  maximum.signal_ms = std::max(maximum.signal_ms, value.signal_ms);
  maximum.acquisition_wakeup_ms = std::max(
      maximum.acquisition_wakeup_ms, value.acquisition_wakeup_ms);
  maximum.digitizer_materialization_ms = std::max(
      maximum.digitizer_materialization_ms, value.digitizer_materialization_ms);
  maximum.session_validation_ms = std::max(
      maximum.session_validation_ms, value.session_validation_ms);
  maximum.enqueue_dispatch_ms = std::max(
      maximum.enqueue_dispatch_ms, value.enqueue_dispatch_ms);
  maximum.queue_wait_ms = std::max(maximum.queue_wait_ms, value.queue_wait_ms);
  maximum.compute_ms = std::max(maximum.compute_ms, value.compute_ms);
}

ProcessingLatencyBreakdown averageLatency(
    const ProcessingLatencyBreakdown& total, std::uint64_t count) {
  if (count == 0U) {
    return {};
  }
  const auto divisor = static_cast<double>(count);
  return {
      total.end_to_end_ms / divisor,
      total.ownership_ms / divisor,
      total.signal_ms / divisor,
      total.acquisition_wakeup_ms / divisor,
      total.digitizer_materialization_ms / divisor,
      total.session_validation_ms / divisor,
      total.enqueue_dispatch_ms / divisor,
      total.queue_wait_ms / divisor,
      total.compute_ms / divisor,
  };
}

double percentile(const std::vector<double>& sorted_values, double quantile) {
  if (sorted_values.empty()) {
    return 0.0;
  }
  const auto index = static_cast<std::size_t>(std::ceil(
      quantile * static_cast<double>(sorted_values.size()))) - 1U;
  return sorted_values[std::min(index, sorted_values.size() - 1U)];
}

}  // namespace

struct ProcessingService::Impl {
  explicit Impl(std::unique_ptr<IFftBackend> backend) : processor(std::move(backend)) {}

  struct PendingRuntimeConfig {
    ProcessingConfig config;
    std::uint64_t revision = 0;
  };

  static bool beginsRasterFrame(const RawFrameBatchPtr& batch) {
    if (!batch || batch->records.empty()) {
      return false;
    }
    const auto& position = batch->records.front().metadata.scan_position;
    return !position.valid || position.y_index == 0U;
  }

  struct BatchTiming {
    std::uint64_t enqueue_timestamp_ns = 0;
    std::uint64_t processing_start_timestamp_ns = 0;
  };

  struct QueuedBatch {
    RawFrameBatchPtr batch;
    BatchTiming timing;
  };

  ProcessingLatencyBreakdown latencyBreakdown(
      const RawFrameBatch& batch, const BatchTiming& timing,
      std::uint64_t line_completed_timestamp_ns) const {
    const auto completion_ns = batch.metadata.completion_timestamp_ns;
    const auto wakeup_ns = orderedTimestamp(
        batch.metadata.acquisition_wakeup_timestamp_ns, completion_ns);
    const auto ownership_ns = orderedTimestamp(
        batch.metadata.ownership_ready_timestamp_ns, wakeup_ns);
    const auto session_ns = orderedTimestamp(
        batch.metadata.session_ready_timestamp_ns, ownership_ns);
    const auto enqueue_ns = orderedTimestamp(timing.enqueue_timestamp_ns, session_ns);
    const auto processing_start_ns = orderedTimestamp(
        timing.processing_start_timestamp_ns, enqueue_ns);
    const auto line_completed_ns = orderedTimestamp(
        line_completed_timestamp_ns, processing_start_ns);

    ProcessingLatencyBreakdown latency;
    latency.end_to_end_ms = elapsedMs(completion_ns, line_completed_ns);
    latency.ownership_ms = elapsedMs(completion_ns, ownership_ns);
    latency.signal_ms = elapsedMs(ownership_ns, line_completed_ns);
    latency.acquisition_wakeup_ms = elapsedMs(completion_ns, wakeup_ns);
    latency.digitizer_materialization_ms = elapsedMs(wakeup_ns, ownership_ns);
    latency.session_validation_ms = elapsedMs(ownership_ns, session_ns);
    latency.enqueue_dispatch_ms = elapsedMs(session_ns, enqueue_ns);
    latency.queue_wait_ms = elapsedMs(enqueue_ns, processing_start_ns);
    latency.compute_ms = elapsedMs(processing_start_ns, line_completed_ns);
    return latency;
  }

  void recordCompletedBatch(const RawFrameBatchPtr& batch,
                            const BatchTiming& timing,
                            std::uint64_t line_completed_timestamp_ns) {
    const auto latency = latencyBreakdown(
        *batch, timing, line_completed_timestamp_ns);
    std::lock_guard<std::mutex> lock(mutex);
    last_latency = latency;
    addLatency(latency_total, latency);
    if (batches_processed == 0U ||
        latency.end_to_end_ms > maximum_latency.end_to_end_ms) {
      maximum_batch_latency_sequence = batch->metadata.sequence;
    }
    if (batches_processed == 0U ||
        latency.acquisition_wakeup_ms > maximum_latency.acquisition_wakeup_ms) {
      maximum_acquisition_wakeup_latency_sequence = batch->metadata.sequence;
    }
    if (batches_processed == 0U || latency.compute_ms > maximum_latency.compute_ms) {
      maximum_compute_latency_sequence = batch->metadata.sequence;
    }
    maximizeLatency(maximum_latency, latency);
    if (latency.end_to_end_ms > kBatchDeadlineMs) {
      ++batch_deadline_misses;
    }
    batch_latency_window.push_back(latency.end_to_end_ms);
    if (batch_latency_window.size() > kLatencyWindowSize) {
      batch_latency_window.pop_front();
    }
    ++batches_processed;
    condition.notify_all();
  }

  bool publishProcessedBatch(const RawFrameBatchPtr& batch,
                              const ProcessedFrameCallback& current_callback,
                              const PointCloudFrameCallback& current_point_cloud_callback,
                              std::uint64_t& line_completed_timestamp_ns,
                              std::string& error) {
    const auto previous_point_cloud = snapshots.latestPointCloud();
    if (!batch || processed_batch_workspace.size() != batch->records.size() ||
        !snapshots.publishBatch(*batch, processed_batch_workspace)) {
      error = "DMA batch snapshot publication received an invalid result count";
      return false;
    }

    line_completed_timestamp_ns = nowNs();
    double batch_record_latency_ms = 0.0;
    bool processing_stop_requested = false;
    for (const auto& processed : processed_batch_workspace) {
      batch_record_latency_ms += processed.processing_latency_ms;
      processing_stop_requested = processing_stop_requested || processed.stop_requested;
    }

    {
      std::lock_guard<std::mutex> lock(mutex);
      frames_processed += processed_batch_workspace.size();
      if (!processed_batch_workspace.empty()) {
        last_processed_frame_id = processed_batch_workspace.back().frame_id;
        processing_config_revision =
            processed_batch_workspace.back().processing_config_revision;
      }
      latency_total_ms += batch_record_latency_ms;
      if (processing_stop_requested) {
        stop_requested = true;
        stop_reason = "Processing requested acquisition stop";
        accepting = false;
        queue.clear();
      }
    }

    if (current_callback) {
      for (auto& processed : processed_batch_workspace) {
        current_callback(std::make_shared<ProcessedFrame>(std::move(processed)));
      }
    }
    const auto completed_point_cloud = snapshots.latestPointCloud();
    if (current_point_cloud_callback && completed_point_cloud &&
        completed_point_cloud != previous_point_cloud && completed_point_cloud->complete) {
      current_point_cloud_callback(completed_point_cloud);
    }
    error.clear();
    return !processing_stop_requested;
  }

  void workerLoop() {
    RealtimeProcessPriorityScope process_priority;
    prioritizeCurrentRealtimeThread(RealtimeThreadPriority::Critical);
    std::string initialization_error;
    const bool initialized = processor.initializeWorkerThread(initialization_error);
    {
      std::lock_guard<std::mutex> lock(mutex);
      worker_ready = true;
      if (!initialized) {
        worker_error = initialization_error.empty()
            ? "Signal processing worker initialization failed"
            : std::move(initialization_error);
        stop_reason = "Signal processing worker initialization failed";
        accepting = false;
        running = false;
      }
      condition.notify_all();
    }
    if (!initialized) {
      return;
    }
    if (processor.supportsAsyncBatchProcessing()) {
      workerLoopAsync();
      return;
    }
    workerLoopSync();
  }

  void workerLoopSync() {
    while (true) {
      QueuedBatch queued_batch;
      std::optional<PendingRuntimeConfig> runtime_update;
      ProcessedFrameCallback current_callback;
      PointCloudFrameCallback current_point_cloud_callback;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return !queue.empty() || !accepting; });
        if (queue.empty() && !accepting) {
          break;
        }
        queued_batch = std::move(queue.front());
        queue.pop_front();
        if (pending_runtime_config.has_value() && beginsRasterFrame(queued_batch.batch)) {
          runtime_update = std::move(pending_runtime_config);
          pending_runtime_config.reset();
        }
        current_callback = callback;
        current_point_cloud_callback = point_cloud_callback;
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
      if (runtime_update.has_value()) {
        std::lock_guard<std::mutex> lock(mutex);
        config.processing = runtime_update->config;
      }
      bool batch_complete = true;
      std::uint64_t line_completed_timestamp_ns = 0U;
      queued_batch.timing.processing_start_timestamp_ns = nowNs();
      if (!processor.processBatch(*queued_batch.batch, snapshots.selectedRecordIndex(),
                                  processed_batch_workspace, error) ||
          processed_batch_workspace.size() != queued_batch.batch->records.size()) {
        std::lock_guard<std::mutex> lock(mutex);
        worker_error = error.empty() ? "DMA batch processing returned an invalid result count" : error;
        stop_reason = "Signal processing failed";
        stop_requested = true;
        accepting = false;
        queue.clear();
        batch_complete = false;
      }
      if (batch_complete &&
          !publishProcessedBatch(queued_batch.batch, current_callback,
                                 current_point_cloud_callback,
                                 line_completed_timestamp_ns, error)) {
        if (!error.empty()) {
          std::lock_guard<std::mutex> lock(mutex);
          worker_error = error;
          stop_reason = "Signal processing failed";
          stop_requested = true;
          accepting = false;
          queue.clear();
        }
        batch_complete = false;
      }
      if (batch_complete) {
        recordCompletedBatch(queued_batch.batch, queued_batch.timing,
                             line_completed_timestamp_ns);
      }
      if (!batch_complete) {
        break;
      }
    }
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
    condition.notify_all();
  }

  void failAsyncWorker(std::string error, std::string reason) {
    std::lock_guard<std::mutex> lock(mutex);
    worker_error = error.empty() ? "Asynchronous CUDA processing failed" : std::move(error);
    stop_reason = std::move(reason);
    stop_requested = true;
    accepting = false;
    queue.clear();
    condition.notify_all();
  }

  bool publishAsyncBatch(const RawFrameBatchPtr& batch,
                          const BatchTiming& timing,
                          const ProcessedFrameCallback& current_callback,
                          const PointCloudFrameCallback& current_point_cloud_callback) {
    std::uint64_t line_completed_timestamp_ns = 0U;
    std::string error;
    if (!publishProcessedBatch(batch, current_callback, current_point_cloud_callback,
                               line_completed_timestamp_ns, error)) {
      if (!error.empty()) {
        failAsyncWorker(std::move(error), "Signal processing failed");
      }
      return false;
    }
    recordCompletedBatch(batch, timing, line_completed_timestamp_ns);
    return true;
  }

  void drainAsyncBatches() {
    while (processor.inFlightBatchCount() > 0U) {
      RawFrameBatchPtr ignored_batch;
      std::vector<ProcessedFrame> ignored_results;
      bool collected = false;
      std::string ignored_error;
      const auto before = processor.inFlightBatchCount();
      processor.collectNextBatch(true, ignored_batch, ignored_results, collected, ignored_error);
      if (processor.inFlightBatchCount() >= before) {
        break;
      }
    }
  }

  void workerLoopAsync() {
    constexpr auto kEventPollInterval = std::chrono::microseconds(100);
    std::deque<BatchTiming> in_flight_timings;
    bool failed = false;
    while (!failed) {
      std::optional<PendingRuntimeConfig> runtime_update;
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending_runtime_config.has_value() && processor.inFlightBatchCount() == 0U &&
            !queue.empty() && beginsRasterFrame(queue.front().batch)) {
          runtime_update = std::move(pending_runtime_config);
          pending_runtime_config.reset();
        }
      }
      if (runtime_update.has_value()) {
        std::string error;
        if (!processor.updateRuntimeConfig(runtime_update->config,
                                           runtime_update->revision, error)) {
          failAsyncWorker(std::move(error), "Runtime processing configuration failed");
          failed = true;
          break;
        }
        std::lock_guard<std::mutex> lock(mutex);
        config.processing = runtime_update->config;
      }

      while (processor.inFlightBatchCount() < processor.asyncBatchCapacity()) {
        QueuedBatch queued_batch;
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (queue.empty() ||
              (pending_runtime_config.has_value() && beginsRasterFrame(queue.front().batch))) {
            break;
          }
          queued_batch = std::move(queue.front());
          queue.pop_front();
        }
        queued_batch.timing.processing_start_timestamp_ns = nowNs();
        std::string error;
        if (!processor.submitBatch(std::move(queued_batch.batch),
                                   snapshots.selectedRecordIndex(), error)) {
          failAsyncWorker(std::move(error), "Signal processing submission failed");
          failed = true;
          break;
        }
        in_flight_timings.push_back(queued_batch.timing);
      }
      if (failed) {
        break;
      }

      const auto in_flight = processor.inFlightBatchCount();
      if (in_flight > 0U) {
        bool must_wait = false;
        {
          std::lock_guard<std::mutex> lock(mutex);
          must_wait = in_flight >= processor.asyncBatchCapacity() ||
              !accepting || pending_runtime_config.has_value();
        }
        std::string error;
        if (!processor.releaseCompletedBatchInputs(must_wait, error)) {
          failAsyncWorker(std::move(error), "CUDA DMA input release failed");
          failed = true;
          break;
        }
        RawFrameBatchPtr completed_batch;
        bool collected = false;
        if (!processor.collectNextBatch(must_wait, completed_batch,
                                        processed_batch_workspace, collected, error)) {
          failAsyncWorker(std::move(error), "Signal processing collection failed");
          failed = true;
          break;
        }
        if (collected) {
          if (in_flight_timings.empty()) {
            failAsyncWorker("CUDA completion has no matching timing record",
                            "Signal processing collection failed");
            failed = true;
            break;
          }
          const auto timing = in_flight_timings.front();
          in_flight_timings.pop_front();
          ProcessedFrameCallback current_callback;
          PointCloudFrameCallback current_point_cloud_callback;
          {
            std::lock_guard<std::mutex> lock(mutex);
            current_callback = callback;
            current_point_cloud_callback = point_cloud_callback;
          }
          if (!publishAsyncBatch(completed_batch, timing, current_callback,
                                 current_point_cloud_callback)) {
            failed = true;
            break;
          }
          continue;
        }

        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, kEventPollInterval, [this] {
          return !queue.empty() || pending_runtime_config.has_value() || !accepting;
        });
        continue;
      }

      std::unique_lock<std::mutex> lock(mutex);
      if (queue.empty() && !accepting) {
        break;
      }
      condition.wait(lock, [this] {
        return !queue.empty() || !accepting;
      });
    }

    drainAsyncBatches();
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
    condition.notify_all();
  }

  mutable std::mutex mutex;
  std::condition_variable condition;
  std::deque<QueuedBatch> queue;
  std::thread worker;
  SignalProcessor processor;
  ProcessingSnapshotStore snapshots;
  std::vector<ProcessedFrame> processed_batch_workspace;
  SystemConfig config;
  std::optional<PendingRuntimeConfig> pending_runtime_config;
  ProcessedFrameCallback callback;
  PointCloudFrameCallback point_cloud_callback;
  std::size_t queue_capacity = 0;
  std::size_t queue_high_water_mark = 0;
  std::uint64_t batches_discarded_on_stop = 0;
  std::uint64_t batches_processed = 0;
  std::uint64_t frames_processed = 0;
  std::uint64_t last_processed_frame_id = 0;
  std::uint64_t processing_config_revision = 0;
  double latency_total_ms = 0.0;
  ProcessingLatencyBreakdown latency_total;
  ProcessingLatencyBreakdown last_latency;
  ProcessingLatencyBreakdown maximum_latency;
  std::uint64_t maximum_batch_latency_sequence = 0;
  std::uint64_t maximum_acquisition_wakeup_latency_sequence = 0;
  std::uint64_t maximum_compute_latency_sequence = 0;
  std::uint64_t batch_deadline_misses = 0;
  std::deque<double> batch_latency_window;
  bool configured = false;
  bool running = false;
  bool worker_ready = false;
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
  impl_->processed_batch_workspace.clear();
  impl_->processed_batch_workspace.resize(config.digitizer.records_per_buffer);
  impl_->configured = true;
  error.clear();
  return true;
}

bool ProcessingService::start(std::string& error) {
  std::unique_lock<std::mutex> lock(impl_->mutex);
  if (!impl_->configured || impl_->running || impl_->worker.joinable()) {
    error = "Processing service is not configured or is already active";
    return false;
  }
  impl_->queue.clear();
  impl_->queue_high_water_mark = 0;
  impl_->batches_discarded_on_stop = 0;
  impl_->batches_processed = 0;
  impl_->frames_processed = 0;
  impl_->last_processed_frame_id = 0;
  impl_->latency_total_ms = 0.0;
  impl_->latency_total = {};
  impl_->last_latency = {};
  impl_->maximum_latency = {};
  impl_->maximum_batch_latency_sequence = 0;
  impl_->maximum_acquisition_wakeup_latency_sequence = 0;
  impl_->maximum_compute_latency_sequence = 0;
  impl_->batch_deadline_misses = 0;
  impl_->batch_latency_window.clear();
  impl_->stop_requested = false;
  impl_->stop_reason.clear();
  impl_->worker_error.clear();
  impl_->worker_ready = false;
  impl_->accepting = true;
  impl_->running = true;
  impl_->worker = std::thread([this] { impl_->workerLoop(); });
  impl_->condition.wait(lock, [this] { return impl_->worker_ready; });
  if (!impl_->worker_error.empty()) {
    error = impl_->worker_error;
    lock.unlock();
    if (impl_->worker.joinable()) {
      impl_->worker.join();
    }
    return false;
  }
  error.clear();
  return true;
}

ProcessingEnqueueResult ProcessingService::enqueueBatch(RawFrameBatchPtr batch, std::string& error) {
  if (!batch || batch->records.empty()) {
    error = "Cannot enqueue a null or empty raw DMA batch";
    return ProcessingEnqueueResult::Error;
  }
  const auto enqueue_timestamp_ns = nowNs();
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
  impl_->queue.push_back({std::move(batch), {enqueue_timestamp_ns, 0U}});
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
  batch->metadata.acquisition_wakeup_timestamp_ns = frame->metadata.host_timestamp_ns;
  batch->metadata.ownership_ready_timestamp_ns = frame->metadata.host_timestamp_ns;
  batch->metadata.session_ready_timestamp_ns = frame->metadata.host_timestamp_ns;
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
  impl_->pending_runtime_config = Impl::PendingRuntimeConfig{config, processing_config_revision};
  impl_->condition.notify_all();
  error.clear();
  return true;
}

void ProcessingService::setProcessedFrameCallback(ProcessedFrameCallback callback) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->callback = std::move(callback);
}

void ProcessingService::setPointCloudFrameCallback(PointCloudFrameCallback callback) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->point_cloud_callback = std::move(callback);
}

void ProcessingService::requestStop(std::string reason, ProcessingStopMode mode) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->running && !impl_->worker.joinable()) {
    return;
  }
  impl_->accepting = false;
  impl_->stop_requested = true;
  if (impl_->stop_reason.empty()) {
    impl_->stop_reason = std::move(reason);
  }
  if (mode == ProcessingStopMode::DiscardPending) {
    impl_->batches_discarded_on_stop += impl_->queue.size();
    impl_->queue.clear();
    impl_->pending_runtime_config.reset();
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

bool ProcessingService::waitForProcessedBatches(std::uint64_t target_count,
                                                std::chrono::milliseconds timeout,
                                                std::string& error) {
  std::unique_lock<std::mutex> lock(impl_->mutex);
  const bool reached = impl_->condition.wait_for(lock, timeout, [this, target_count] {
    return impl_->batches_processed >= target_count || !impl_->running ||
        !impl_->worker_error.empty();
  });
  if (!reached || impl_->batches_processed < target_count) {
    error = impl_->worker_error.empty()
        ? "Timed out waiting for processed DMA batches"
        : impl_->worker_error;
    return false;
  }
  error.clear();
  return true;
}

bool ProcessingService::stopRequested(std::string& reason) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  reason = impl_->stop_reason;
  return impl_->stop_requested;
}

ProcessingServiceStatus ProcessingService::status() const {
  ProcessingServiceStatus status;
  std::vector<double> latency_values;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    status.configured = impl_->configured;
    status.running = impl_->running;
    status.stop_requested = impl_->stop_requested;
    status.queue_size = impl_->queue.size();
    status.queue_capacity = impl_->queue_capacity;
    status.queue_high_water_mark = impl_->queue_high_water_mark;
    status.batches_discarded_on_stop = impl_->batches_discarded_on_stop;
    status.batches_processed = impl_->batches_processed;
    status.frames_processed = impl_->frames_processed;
    status.last_processed_frame_id = impl_->last_processed_frame_id;
    status.processing_config_revision = impl_->processing_config_revision;
    status.average_latency_ms = impl_->frames_processed == 0U
        ? 0.0
        : impl_->latency_total_ms / static_cast<double>(impl_->frames_processed);
    status.last_latency = impl_->last_latency;
    status.average_latency = averageLatency(
        impl_->latency_total, impl_->batches_processed);
    status.maximum_latency = impl_->maximum_latency;
    status.average_batch_latency_ms = status.average_latency.end_to_end_ms;
    status.last_batch_latency_ms = status.last_latency.end_to_end_ms;
    status.last_ownership_copy_latency_ms = status.last_latency.ownership_ms;
    status.last_signal_processing_latency_ms = status.last_latency.signal_ms;
    status.maximum_ownership_copy_latency_ms = status.maximum_latency.ownership_ms;
    status.maximum_signal_processing_latency_ms = status.maximum_latency.signal_ms;
    status.maximum_batch_latency_ms = status.maximum_latency.end_to_end_ms;
    status.maximum_batch_latency_sequence = impl_->maximum_batch_latency_sequence;
    status.maximum_acquisition_wakeup_latency_sequence =
        impl_->maximum_acquisition_wakeup_latency_sequence;
    status.maximum_compute_latency_sequence = impl_->maximum_compute_latency_sequence;
    status.batch_deadline_ms = kBatchDeadlineMs;
    status.batch_deadline_misses = impl_->batch_deadline_misses;
    status.backend_name = impl_->processor.backendName();
    status.stop_reason = impl_->stop_reason;
    latency_values.assign(impl_->batch_latency_window.begin(), impl_->batch_latency_window.end());
  }
  std::sort(latency_values.begin(), latency_values.end());
  status.batch_latency_p50_ms = percentile(latency_values, 0.50);
  status.batch_latency_p95_ms = percentile(latency_values, 0.95);
  status.batch_latency_p99_ms = percentile(latency_values, 0.99);
  return status;
}

ProcessingSnapshotStore& ProcessingService::snapshots() { return impl_->snapshots; }

const ProcessingSnapshotStore& ProcessingService::snapshots() const { return impl_->snapshots; }

void ProcessingService::setSelectedRecordIndex(std::uint32_t record_index) {
  impl_->snapshots.setSelectedRecordIndex(record_index);
}

}  // namespace fmcw
