#include "detection/object_detection_service.h"

#include "detection/centerpoint_input_builder.h"

#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace fmcw {

struct ObjectDetectionService::Impl {
  explicit Impl(std::unique_ptr<ObjectDetector> backend)
      : detector(std::move(backend)) {}

  void workerLoop() {
    for (;;) {
      std::shared_ptr<const PointCloudSnapshot> snapshot;
      {
        std::unique_lock lock(mutex);
        wake.wait(lock, [this] { return stop_requested || pending_snapshot != nullptr; });
        if (stop_requested) {
          break;
        }
        snapshot = std::move(pending_snapshot);
      }

      DetectionSnapshotPtr result;
      try {
        auto input = buildCenterPointInput(*snapshot, config.input);
        result = std::make_shared<DetectionSnapshot>(detector->detect(input));
      } catch (const std::exception& exception) {
        auto failed = std::make_shared<DetectionSnapshot>();
        failed->last_frame_id = snapshot->last_frame_id;
        failed->scan_frame_index = snapshot->scan_frame_index;
        failed->processing_config_revision = snapshot->processing_config_revision;
        failed->source_frame_complete = snapshot->complete;
        failed->status = exception.what();
        result = std::move(failed);
      }

      bool backend_failed = false;
      {
        std::lock_guard lock(mutex);
        latest_snapshot = std::move(result);
        ++service_status.frames_processed;
        service_status.backend_ready = detector->ready();
        service_status.detail = latest_snapshot->status;
        backend_failed = !service_status.backend_ready;
      }
      if (backend_failed) {
        break;
      }
    }

    std::lock_guard lock(mutex);
    service_status.running = false;
  }

  mutable std::mutex mutex;
  std::condition_variable wake;
  std::unique_ptr<ObjectDetector> detector;
  ObjectDetectorConfig config;
  std::thread worker;
  std::shared_ptr<const PointCloudSnapshot> pending_snapshot;
  DetectionSnapshotPtr latest_snapshot;
  ObjectDetectionServiceStatus service_status;
  bool stop_requested = false;
};

ObjectDetectionService::ObjectDetectionService(std::unique_ptr<ObjectDetector> detector)
    : impl_(std::make_unique<Impl>(std::move(detector))) {}

ObjectDetectionService::~ObjectDetectionService() {
  stop();
}

bool ObjectDetectionService::initialize(const ObjectDetectorConfig& config,
                                        std::string& error) {
  stop();
  if (impl_->detector == nullptr) {
    error = "Object detection service requires a detector backend";
    return false;
  }
  if (!impl_->detector->initialize(config, error)) {
    std::lock_guard lock(impl_->mutex);
    impl_->service_status = {};
    impl_->service_status.detail = error;
    return false;
  }

  std::lock_guard lock(impl_->mutex);
  impl_->config = config;
  impl_->pending_snapshot.reset();
  impl_->latest_snapshot.reset();
  impl_->service_status = {};
  impl_->service_status.initialized = true;
  impl_->service_status.backend_ready = true;
  impl_->service_status.detail = impl_->detector->backendName();
  error.clear();
  return true;
}

bool ObjectDetectionService::start(std::string& error) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->service_status.running) {
    error.clear();
    return true;
  }
  if (!impl_->service_status.initialized || !impl_->detector->ready()) {
    error = "Initialize a ready object detector before starting inference";
    return false;
  }
  if (impl_->worker.joinable()) {
    error = "Previous object detection worker has not been joined";
    return false;
  }
  impl_->stop_requested = false;
  impl_->service_status.running = true;
  impl_->worker = std::thread([this] { impl_->workerLoop(); });
  error.clear();
  return true;
}

DetectionEnqueueResult ObjectDetectionService::enqueue(
    std::shared_ptr<const PointCloudSnapshot> snapshot) {
  if (snapshot == nullptr || !snapshot->complete) {
    return DetectionEnqueueResult::InvalidFrame;
  }
  std::lock_guard lock(impl_->mutex);
  if (!impl_->service_status.running || impl_->stop_requested) {
    return DetectionEnqueueResult::NotRunning;
  }
  const bool replaced = impl_->pending_snapshot != nullptr;
  if (replaced) {
    ++impl_->service_status.frames_replaced;
  }
  impl_->pending_snapshot = std::move(snapshot);
  ++impl_->service_status.frames_submitted;
  impl_->wake.notify_one();
  return replaced ? DetectionEnqueueResult::ReplacedPending
                  : DetectionEnqueueResult::Accepted;
}

void ObjectDetectionService::stop() {
  {
    std::lock_guard lock(impl_->mutex);
    impl_->stop_requested = true;
    impl_->pending_snapshot.reset();
  }
  impl_->wake.notify_all();
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  std::lock_guard lock(impl_->mutex);
  impl_->service_status.running = false;
}

DetectionSnapshotPtr ObjectDetectionService::latestSnapshot() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->latest_snapshot;
}

ObjectDetectionServiceStatus ObjectDetectionService::status() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->service_status;
}

}  // namespace fmcw
