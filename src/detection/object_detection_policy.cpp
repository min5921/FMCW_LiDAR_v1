#include "detection/object_detection_policy.h"

namespace fmcw {

ObjectDetectionGate evaluateObjectDetectionGate(bool configured,
                                                FftBackendKind fft_backend,
                                                bool detector_compiled,
                                                bool cuda_runtime_available) {
  if (!configured) {
    return {false, "Apply a processing configuration before enabling object detection"};
  }
  if (fft_backend != FftBackendKind::Cuda) {
    return {false, "Object detection is available only while CUDA cuFFT processing is selected"};
  }
  if (!detector_compiled) {
    return {false, "This application was built without the CenterPoint backend"};
  }
  if (!cuda_runtime_available) {
    return {false, "No CUDA device is available for cuFFT and CenterPoint"};
  }
  return {true, "CUDA cuFFT processing and CenterPoint are available"};
}

}  // namespace fmcw
