#pragma once

#include "processing/processing_snapshots.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fmcw {

enum class CenterPointFifthFeature : std::uint8_t {
  Zero,
  Velocity,
};

struct CenterPointInputConfig {
  // Keep the pretrained Waymo grid until the cropped-grid CUDA path is parameterized.
  std::array<float, 3> voxel_size_m{0.32F, 0.32F, 6.0F};
  std::array<float, 6> point_cloud_range_m{
      -74.88F, -74.88F, -2.0F, 74.88F, 74.88F, 4.0F};
  // Initial fixed mapping for FMCW dB values. Calibrate these bounds from recorded
  // point statistics before judging pretrained-model accuracy.
  float intensity_min_db = -100.0F;
  float intensity_max_db = 0.0F;
  CenterPointFifthFeature fifth_feature = CenterPointFifthFeature::Zero;
};

struct CenterPointInput {
  static constexpr int kFeatureDimension = 5;

  std::uint64_t last_frame_id = 0;
  std::uint64_t scan_frame_index = 0;
  std::uint64_t processing_config_revision = 0;
  bool source_frame_complete = false;
  std::size_t source_points = 0;
  std::size_t discarded_invalid_points = 0;
  std::size_t discarded_out_of_range_points = 0;
  std::vector<float> features;

  std::size_t pointCount() const {
    return features.size() / static_cast<std::size_t>(kFeatureDimension);
  }
};

bool isValid(const CenterPointInputConfig& config);
float normalizeCenterPointIntensity(float intensity_db,
                                    const CenterPointInputConfig& config);
CenterPointInput buildCenterPointInput(const PointCloudSnapshot& snapshot,
                                       const CenterPointInputConfig& config = {});

}  // namespace fmcw
