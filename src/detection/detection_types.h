#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fmcw {

enum class ObjectClass : std::uint8_t {
  Vehicle = 0,
  Pedestrian = 1,
  Cyclist = 2,
  Unknown = 255,
};

inline const char* toString(ObjectClass object_class) {
  switch (object_class) {
    case ObjectClass::Vehicle:
      return "Vehicle";
    case ObjectClass::Pedestrian:
      return "Pedestrian";
    case ObjectClass::Cyclist:
      return "Cyclist";
    case ObjectClass::Unknown:
      return "Unknown";
  }
  return "Unknown";
}

struct Box3D {
  // ROS/RViz right-handed axes: +X forward, +Y left, +Z up.
  float center_x_m = 0.0F;
  float center_y_m = 0.0F;
  float center_z_m = 0.0F;
  float length_x_m = 0.0F;
  float width_y_m = 0.0F;
  float height_z_m = 0.0F;
  float yaw_rad = 0.0F;
  float score = 0.0F;
  ObjectClass object_class = ObjectClass::Unknown;
  int source_index = -1;
};

struct DetectionTiming {
  float preprocess_ms = 0.0F;
  float rpn_ms = 0.0F;
  float head_ms = 0.0F;
  float postprocess_ms = 0.0F;
  float total_ms = 0.0F;
};

struct DetectionSnapshot {
  std::uint64_t last_frame_id = 0;
  std::uint64_t scan_frame_index = 0;
  std::uint64_t processing_config_revision = 0;
  std::size_t input_points = 0;
  std::size_t accepted_points = 0;
  bool source_frame_complete = false;
  bool backend_ready = false;
  DetectionTiming timing;
  std::vector<Box3D> boxes;
  std::string status;
};

using DetectionSnapshotPtr = std::shared_ptr<const DetectionSnapshot>;

}  // namespace fmcw
