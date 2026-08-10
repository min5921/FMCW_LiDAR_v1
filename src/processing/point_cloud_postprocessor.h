#pragma once

#include "processing/processing_snapshots.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace fmcw {

struct PointCloudDisplayPoint {
  PointXYZI point;
  std::uint8_t temporal_observations = 0U;
  bool interpolated = false;
};

struct PointCloudDisplayFrame {
  std::uint64_t scan_frame_index = 0U;
  std::uint32_t source_width = 0U;
  std::uint32_t source_height = 0U;
  std::uint32_t display_width = 0U;
  std::uint32_t display_height = 0U;
  std::size_t source_valid_point_count = 0U;
  std::size_t fused_point_count = 0U;
  std::size_t interpolated_point_count = 0U;
  std::vector<PointCloudDisplayPoint> points;

  std::size_t displayedPointCount() const {
    return fused_point_count + interpolated_point_count;
  }
};

// Display-only post-processing for complete organized raster frames. This class never
// changes the acquisition, processing, storage, or UDP point contracts.
class PointCloudPostProcessor {
 public:
  void setHistoryFrameCount(std::uint32_t frame_count);
  std::uint32_t historyFrameCount() const;
  void setVerticalInterpolationFactor(std::uint32_t factor);
  std::uint32_t verticalInterpolationFactor() const;

  bool push(std::shared_ptr<const PointCloudSnapshot> snapshot);
  void reset();

  const PointCloudDisplayFrame& displayFrame() const;

 private:
  void rebuild();

  std::uint32_t history_frame_count_ = 1U;
  std::uint32_t vertical_interpolation_factor_ = 1U;
  std::deque<std::shared_ptr<const PointCloudSnapshot>> history_;
  PointCloudDisplayFrame display_frame_;
};

}  // namespace fmcw
