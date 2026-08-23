#pragma once

#include "core/config_types.h"

#include <string>

namespace fmcw {

struct ObjectDetectionGate {
  bool can_enable = false;
  std::string reason;
};

ObjectDetectionGate evaluateObjectDetectionGate(bool configured,
                                                FftBackendKind fft_backend,
                                                bool detector_compiled,
                                                bool cuda_runtime_available);

}  // namespace fmcw
