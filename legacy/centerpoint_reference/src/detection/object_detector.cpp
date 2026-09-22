#include "detection/object_detector.h"

#include <cmath>
#include <system_error>

namespace fmcw {
namespace {

bool requiredFileExists(const std::filesystem::path& path, std::string& error) {
  std::error_code filesystem_error;
  const bool exists = std::filesystem::is_regular_file(path, filesystem_error);
  if (filesystem_error) {
    error = "unable to inspect CenterPoint weight file " + path.string() +
        ": " + filesystem_error.message();
    return false;
  }
  if (!exists) {
    error = "missing CenterPoint weight file: " + path.string();
    return false;
  }
  return true;
}

bool unitValue(float value) {
  return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

}  // namespace

bool validateObjectDetectorConfig(const ObjectDetectorConfig& config,
                                  std::string& error) {
  error.clear();
  if (!isValid(config.input)) {
    error = "invalid CenterPoint point-cloud input configuration";
    return false;
  }
  if (config.weights_root.empty()) {
    error = "CenterPoint weights root is empty";
    return false;
  }
  std::error_code filesystem_error;
  if (!std::filesystem::is_directory(config.weights_root, filesystem_error)) {
    error = filesystem_error
        ? "unable to inspect CenterPoint weights root: " + filesystem_error.message()
        : "CenterPoint weights root is not a directory: " + config.weights_root.string();
    return false;
  }
  const std::array<std::filesystem::path, 3> manifests{
      config.weights_root / "04_pfn" / "weights_metadata.json",
      config.weights_root / "06_rpn" / "rpn_weights_metadata.json",
      config.weights_root / "07_head" / "head_weights_metadata.json",
  };
  for (const auto& manifest : manifests) {
    if (!requiredFileExists(manifest, error)) {
      return false;
    }
  }
  if (!unitValue(config.score_threshold) ||
      !unitValue(config.nms_iou_threshold)) {
    error = "CenterPoint score and NMS thresholds must be finite values in [0, 1]";
    return false;
  }
  for (float threshold : config.class_score_thresholds) {
    if (!unitValue(threshold)) {
      error = "CenterPoint class score thresholds must be finite values in [0, 1]";
      return false;
    }
  }
  if (config.maximum_detections <= 0 || config.maximum_detections > 500) {
    error = "CenterPoint maximum detections must be in [1, 500]";
    return false;
  }
  return true;
}

}  // namespace fmcw
