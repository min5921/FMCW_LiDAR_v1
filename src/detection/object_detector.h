#pragma once

#include "detection/centerpoint_input_builder.h"
#include "detection/detection_types.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace fmcw {

struct ObjectDetectorConfig {
  std::filesystem::path weights_root;
  CenterPointInputConfig input;
  float score_threshold = 0.35F;
  std::array<float, 3> class_score_thresholds{0.35F, 0.35F, 0.35F};
  bool use_class_score_thresholds = false;
  float nms_iou_threshold = 0.5F;
  int maximum_detections = 500;
};

class ObjectDetector {
 public:
  virtual ~ObjectDetector() = default;

  ObjectDetector(const ObjectDetector&) = delete;
  ObjectDetector& operator=(const ObjectDetector&) = delete;

  virtual const char* backendName() const = 0;
  virtual bool initialize(const ObjectDetectorConfig& config, std::string& error) = 0;
  virtual bool ready() const = 0;
  virtual DetectionSnapshot detect(const CenterPointInput& input) = 0;

 protected:
  ObjectDetector() = default;
};

bool validateObjectDetectorConfig(const ObjectDetectorConfig& config,
                                  std::string& error);
bool centerPointBackendCompiled();
std::unique_ptr<ObjectDetector> createCenterPointObjectDetector();

}  // namespace fmcw
