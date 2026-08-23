#pragma once

#include "detection/detection_types.h"
#include "detection/object_detector.h"
#include "processing/processing_snapshots.h"

#include <cstdint>
#include <memory>
#include <string>

namespace fmcw {

enum class DetectionEnqueueResult {
  Accepted,
  ReplacedPending,
  NotRunning,
  InvalidFrame,
};

struct ObjectDetectionServiceStatus {
  bool initialized = false;
  bool running = false;
  bool backend_ready = false;
  std::uint64_t frames_submitted = 0;
  std::uint64_t frames_processed = 0;
  std::uint64_t frames_replaced = 0;
  std::string detail;
};

class ObjectDetectionService {
 public:
  explicit ObjectDetectionService(std::unique_ptr<ObjectDetector> detector);
  ~ObjectDetectionService();

  ObjectDetectionService(const ObjectDetectionService&) = delete;
  ObjectDetectionService& operator=(const ObjectDetectionService&) = delete;

  bool initialize(const ObjectDetectorConfig& config, std::string& error);
  bool start(std::string& error);
  DetectionEnqueueResult enqueue(std::shared_ptr<const PointCloudSnapshot> snapshot);
  void stop();

  DetectionSnapshotPtr latestSnapshot() const;
  ObjectDetectionServiceStatus status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fmcw
