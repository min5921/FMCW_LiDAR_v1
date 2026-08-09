#include "detection/object_detector.h"

#include "centerpoint/gpu_center_head.hpp"
#include "centerpoint/gpu_postprocess.hpp"
#include "centerpoint/gpu_preprocess.hpp"
#include "centerpoint/gpu_rpn.hpp"
#include "centerpoint/head_weights.hpp"
#include "centerpoint/pfn_weights.hpp"
#include "centerpoint/rpn_weights.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace fmcw {
namespace {

constexpr std::array<float, 3> kValidatedVoxelSize{0.32F, 0.32F, 6.0F};
constexpr std::array<float, 6> kValidatedPointCloudRange{
    -74.88F, -74.88F, -2.0F, 74.88F, 74.88F, 4.0F};

template <std::size_t Size>
bool matchesValidatedValues(const std::array<float, Size>& actual,
                            const std::array<float, Size>& expected) {
  for (std::size_t index = 0; index < Size; ++index) {
    if (std::abs(actual[index] - expected[index]) > 1.0e-5F) {
      return false;
    }
  }
  return true;
}

ObjectClass objectClassFromLabel(int label) {
  switch (label) {
    case 0:
      return ObjectClass::Vehicle;
    case 1:
      return ObjectClass::Pedestrian;
    case 2:
      return ObjectClass::Cyclist;
    default:
      return ObjectClass::Unknown;
  }
}

class CenterPointObjectDetector final : public ObjectDetector {
 public:
  const char* backendName() const override {
    return "CenterPoint CUDA/cuDNN active-count NMS";
  }

  bool initialize(const ObjectDetectorConfig& config, std::string& error) override {
    ready_ = false;
    postprocess_.reset();
    head_.reset();
    rpn_.reset();
    preprocess_.reset();

    if (!validateObjectDetectorConfig(config, error)) {
      return false;
    }
    if (config.input.fifth_feature != CenterPointFifthFeature::Zero) {
      error = "the selected no-velocity CenterPoint weights require fifth feature = 0";
      return false;
    }
    if (!matchesValidatedValues(config.input.voxel_size_m, kValidatedVoxelSize) ||
        !matchesValidatedValues(config.input.point_cloud_range_m,
                                kValidatedPointCloudRange)) {
      error = "the current CenterPoint CUDA runtime requires the validated 468x468 grid";
      return false;
    }

    try {
      const auto pfn_weights = centerpoint::load_pfn_weights(
          config.weights_root / "04_pfn");
      const auto rpn_weights = centerpoint::load_rpn_weights(
          config.weights_root / "06_rpn");
      const auto head_weights = centerpoint::load_head_weights(
          config.weights_root / "07_head");

      centerpoint::GpuPreprocessConfig preprocess_config;
      preprocess_config.voxel_size = config.input.voxel_size_m;
      preprocess_config.point_cloud_range = config.input.point_cloud_range_m;
      preprocess_config.feature_dimension = CenterPointInput::kFeatureDimension;

      centerpoint::GpuPostprocessConfig postprocess_config;
      postprocess_config.score_threshold = config.score_threshold;
      postprocess_config.class_score_thresholds = config.class_score_thresholds;
      postprocess_config.use_class_score_thresholds =
          config.use_class_score_thresholds;
      postprocess_config.nms_iou_threshold = config.nms_iou_threshold;
      postprocess_config.use_pcdet_nms_convention = true;
      postprocess_config.point_cloud_x = config.input.point_cloud_range_m[0];
      postprocess_config.point_cloud_y = config.input.point_cloud_range_m[1];
      postprocess_config.cell_x = config.input.voxel_size_m[0];
      postprocess_config.cell_y = config.input.voxel_size_m[1];
      postprocess_config.post_max_size = config.maximum_detections;

      auto preprocess = std::make_unique<centerpoint::GpuPreprocessPipeline>(
          preprocess_config, pfn_weights);
      auto rpn = std::make_unique<centerpoint::GpuRpnPipeline>(rpn_weights);
      auto head = std::make_unique<centerpoint::GpuCenterHeadPipeline>(head_weights);
      auto postprocess = std::make_unique<centerpoint::GpuPostprocessPipeline>(
          postprocess_config);

      config_ = config;
      preprocess_ = std::move(preprocess);
      rpn_ = std::move(rpn);
      head_ = std::move(head);
      postprocess_ = std::move(postprocess);
      ready_ = true;
      error.clear();
      return true;
    } catch (const std::exception& exception) {
      error = exception.what();
      return false;
    }
  }

  bool ready() const override {
    return ready_;
  }

  DetectionSnapshot detect(const CenterPointInput& input) override {
    DetectionSnapshot snapshot;
    snapshot.last_frame_id = input.last_frame_id;
    snapshot.scan_frame_index = input.scan_frame_index;
    snapshot.processing_config_revision = input.processing_config_revision;
    snapshot.input_points = input.source_points;
    snapshot.accepted_points = input.pointCount();
    snapshot.source_frame_complete = input.source_frame_complete;
    snapshot.backend_ready = ready_;

    if (!ready_) {
      snapshot.status = "CenterPoint backend is not initialized";
      return snapshot;
    }
    if (!input.source_frame_complete) {
      snapshot.status = "inference requires a complete point-cloud frame";
      return snapshot;
    }
    if (input.features.size() % CenterPointInput::kFeatureDimension != 0U) {
      snapshot.status = "CenterPoint input feature buffer is not divisible by five";
      return snapshot;
    }
    if (input.pointCount() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      snapshot.status = "CenterPoint input contains too many points";
      return snapshot;
    }
    if (input.pointCount() == 0U) {
      snapshot.status = "complete frame contains no accepted points";
      return snapshot;
    }

    const auto wall_start = std::chrono::steady_clock::now();
    try {
      preprocess_->enqueue(input.features.data(), static_cast<int>(input.pointCount()));
      rpn_->enqueue(preprocess_->device_bev(), false);
      head_->enqueue(rpn_->device_output(), false);
      postprocess_->enqueue(head_->device_maps());

      const auto postprocess_stats = postprocess_->collect_stats();
      const auto head_stats = head_->collect_stats();
      const auto rpn_stats = rpn_->collect_stats();
      const auto preprocess_stats = preprocess_->collect_stats();
      const auto detections = postprocess_->copy_detections_to_host();

      snapshot.timing.preprocess_ms = preprocess_stats.total_ms;
      snapshot.timing.rpn_ms = rpn_stats.elapsed_ms;
      snapshot.timing.head_ms = head_stats.elapsed_ms;
      snapshot.timing.postprocess_ms = postprocess_stats.elapsed_ms;
      snapshot.boxes.reserve(detections.size());
      for (const auto& detection : detections) {
        Box3D box;
        box.center_x_m = detection.x;
        box.center_y_m = detection.y;
        box.center_z_m = detection.z;
        box.length_x_m = detection.dx;
        box.width_y_m = detection.dy;
        box.height_z_m = detection.dz;
        box.yaw_rad = detection.yaw;
        box.score = detection.score;
        box.object_class = objectClassFromLabel(detection.label);
        box.source_index = detection.source_index;
        snapshot.boxes.push_back(box);
      }
      snapshot.backend_ready = true;
      snapshot.status = "ok";
    } catch (const std::exception& exception) {
      ready_ = false;
      snapshot.backend_ready = false;
      snapshot.status = exception.what();
    }
    const auto wall_stop = std::chrono::steady_clock::now();
    snapshot.timing.total_ms = static_cast<float>(
        std::chrono::duration<double, std::milli>(wall_stop - wall_start).count());
    return snapshot;
  }

 private:
  ObjectDetectorConfig config_;
  std::unique_ptr<centerpoint::GpuPreprocessPipeline> preprocess_;
  std::unique_ptr<centerpoint::GpuRpnPipeline> rpn_;
  std::unique_ptr<centerpoint::GpuCenterHeadPipeline> head_;
  std::unique_ptr<centerpoint::GpuPostprocessPipeline> postprocess_;
  bool ready_ = false;
};

}  // namespace

bool centerPointBackendCompiled() {
  return true;
}

std::unique_ptr<ObjectDetector> createCenterPointObjectDetector() {
  return std::make_unique<CenterPointObjectDetector>();
}

}  // namespace fmcw
