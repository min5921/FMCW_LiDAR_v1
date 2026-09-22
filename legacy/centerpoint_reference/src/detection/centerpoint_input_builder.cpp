#include "detection/centerpoint_input_builder.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fmcw {
namespace {

bool pointInRange(const PointXYZI& point, const CenterPointInputConfig& config) {
  const auto& range = config.point_cloud_range_m;
  return point.x >= range[0] && point.x < range[3] &&
      point.y >= range[1] && point.y < range[4] &&
      point.z >= range[2] && point.z < range[5];
}

}  // namespace

bool isValid(const CenterPointInputConfig& config) {
  const auto& voxel = config.voxel_size_m;
  const auto& range = config.point_cloud_range_m;
  return std::all_of(voxel.begin(), voxel.end(),
                     [](float value) { return std::isfinite(value) && value > 0.0F; }) &&
      std::all_of(range.begin(), range.end(),
                  [](float value) { return std::isfinite(value); }) &&
      range[0] < range[3] && range[1] < range[4] && range[2] < range[5] &&
      std::isfinite(config.intensity_min_db) &&
      std::isfinite(config.intensity_max_db) &&
      config.intensity_min_db < config.intensity_max_db;
}

float normalizeCenterPointIntensity(float intensity_db,
                                    const CenterPointInputConfig& config) {
  if (!std::isfinite(intensity_db) || !isValid(config)) {
    return 0.0F;
  }
  const auto normalized = (intensity_db - config.intensity_min_db) /
      (config.intensity_max_db - config.intensity_min_db);
  return std::clamp(normalized, 0.0F, 1.0F);
}

CenterPointInput buildCenterPointInput(const PointCloudSnapshot& snapshot,
                                       const CenterPointInputConfig& config) {
  if (!isValid(config)) {
    throw std::invalid_argument("invalid CenterPoint input configuration");
  }

  CenterPointInput input;
  input.last_frame_id = snapshot.last_frame_id;
  input.scan_frame_index = snapshot.scan_frame_index;
  input.processing_config_revision = snapshot.processing_config_revision;
  input.source_frame_complete = snapshot.complete;
  input.source_points = snapshot.points.size();
  input.features.reserve(snapshot.points.size() * CenterPointInput::kFeatureDimension);

  for (const auto& point : snapshot.points) {
    if (!point.valid || !std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(point.z) || !std::isfinite(point.intensity)) {
      ++input.discarded_invalid_points;
      continue;
    }
    if (!pointInRange(point, config)) {
      ++input.discarded_out_of_range_points;
      continue;
    }

    input.features.push_back(point.x);
    input.features.push_back(point.y);
    input.features.push_back(point.z);
    const bool waymo_features = snapshot.feature_encoding ==
        PointCloudFeatureEncoding::CenterPointWaymo;
    input.features.push_back(waymo_features
        ? point.intensity
        : normalizeCenterPointIntensity(point.intensity, config));
    const auto fifth = waymo_features && std::isfinite(point.elongation)
        ? point.elongation
        : config.fifth_feature == CenterPointFifthFeature::Velocity &&
                std::isfinite(point.velocity)
            ? point.velocity
            : 0.0F;
    input.features.push_back(fifth);
  }

  return input;
}

}  // namespace fmcw
