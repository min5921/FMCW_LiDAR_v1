#include "detection/object_detector.h"

namespace fmcw {

bool centerPointBackendCompiled() {
  return false;
}

std::unique_ptr<ObjectDetector> createCenterPointObjectDetector() {
  return {};
}

}  // namespace fmcw
