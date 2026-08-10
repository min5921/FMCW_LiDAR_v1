#include "processing/point_cloud_postprocessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace fmcw {
namespace {

constexpr std::uint32_t kMinimumHistoryFrames = 1U;
constexpr std::uint32_t kMaximumHistoryFrames = 5U;
constexpr float kMinimumRangeJumpGateM = 0.05F;
constexpr float kAngularSpacingGateScale = 3.0F;

bool finitePoint(const PointXYZI& point) {
  return point.valid && std::isfinite(point.x) && std::isfinite(point.y) &&
      std::isfinite(point.z) && std::isfinite(point.intensity) &&
      std::isfinite(point.velocity) && std::isfinite(point.scan_x_command) &&
      std::isfinite(point.scan_y_command);
}

template <std::size_t Capacity>
float median(std::array<float, Capacity> values, std::size_t count) {
  if (count == 0U) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
  const auto middle = count / 2U;
  if ((count & 1U) != 0U) {
    return values[middle];
  }
  return 0.5F * (values[middle - 1U] + values[middle]);
}

PointCloudDisplayPoint fuseCell(
    const std::deque<std::shared_ptr<const PointCloudSnapshot>>& history,
    std::size_t point_index) {
  std::array<float, kMaximumHistoryFrames> x{};
  std::array<float, kMaximumHistoryFrames> y{};
  std::array<float, kMaximumHistoryFrames> z{};
  std::array<float, kMaximumHistoryFrames> intensity{};
  std::array<float, kMaximumHistoryFrames> velocity{};
  std::array<float, kMaximumHistoryFrames> scan_x{};
  std::array<float, kMaximumHistoryFrames> scan_y{};
  std::size_t count = 0U;
  for (const auto& snapshot : history) {
    if (point_index >= snapshot->points.size()) {
      continue;
    }
    const auto& point = snapshot->points[point_index];
    if (!finitePoint(point)) {
      continue;
    }
    x[count] = point.x;
    y[count] = point.y;
    z[count] = point.z;
    intensity[count] = point.intensity;
    velocity[count] = point.velocity;
    scan_x[count] = point.scan_x_command;
    scan_y[count] = point.scan_y_command;
    ++count;
  }

  PointCloudDisplayPoint result;
  if (count == 0U) {
    return result;
  }
  result.point.x = median(x, count);
  result.point.y = median(y, count);
  result.point.z = median(z, count);
  result.point.intensity = median(intensity, count);
  result.point.velocity = median(velocity, count);
  result.point.scan_x_command = median(scan_x, count);
  result.point.scan_y_command = median(scan_y, count);
  result.point.valid = true;
  result.temporal_observations = static_cast<std::uint8_t>(count);
  return result;
}

float pointRange(const PointXYZI& point) {
  return std::sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
}

bool edgeCompatible(const PointXYZI& first, const PointXYZI& second) {
  if (!finitePoint(first) || !finitePoint(second)) {
    return false;
  }
  const auto first_range = pointRange(first);
  const auto second_range = pointRange(second);
  if (!(first_range > 0.0F) || !(second_range > 0.0F)) {
    return false;
  }
  const auto dot = std::clamp(
      (first.x * second.x + first.y * second.y + first.z * second.z) /
          (first_range * second_range),
      -1.0F, 1.0F);
  const auto angular_spacing = std::acos(dot);
  const auto expected_surface_spacing = std::min(first_range, second_range) * angular_spacing;
  const auto range_jump_gate = std::max(
      kMinimumRangeJumpGateM, kAngularSpacingGateScale * expected_surface_spacing);
  return std::abs(first_range - second_range) <= range_jump_gate;
}

PointCloudDisplayPoint interpolate(const PointCloudDisplayPoint& first,
                                   const PointCloudDisplayPoint& second,
                                   float fraction) {
  PointCloudDisplayPoint result;
  if (!edgeCompatible(first.point, second.point)) {
    return result;
  }
  const auto blend = [fraction](float start, float end) {
    return start + (end - start) * fraction;
  };
  result.point.x = blend(first.point.x, second.point.x);
  result.point.y = blend(first.point.y, second.point.y);
  result.point.z = blend(first.point.z, second.point.z);
  result.point.intensity = blend(first.point.intensity, second.point.intensity);
  result.point.velocity = blend(first.point.velocity, second.point.velocity);
  result.point.scan_x_command = blend(first.point.scan_x_command, second.point.scan_x_command);
  result.point.scan_y_command = blend(first.point.scan_y_command, second.point.scan_y_command);
  result.point.valid = true;
  result.temporal_observations = std::min(first.temporal_observations,
                                          second.temporal_observations);
  result.interpolated = true;
  return result;
}

std::uint32_t normalizedInterpolationFactor(std::uint32_t factor) {
  if (factor >= 4U) {
    return 4U;
  }
  if (factor >= 2U) {
    return 2U;
  }
  return 1U;
}

}  // namespace

void PointCloudPostProcessor::setHistoryFrameCount(std::uint32_t frame_count) {
  const auto bounded = std::clamp(frame_count, kMinimumHistoryFrames,
                                  kMaximumHistoryFrames);
  if (bounded == history_frame_count_) {
    return;
  }
  history_frame_count_ = bounded;
  while (history_.size() > history_frame_count_) {
    history_.pop_front();
  }
  rebuild();
}

std::uint32_t PointCloudPostProcessor::historyFrameCount() const {
  return history_frame_count_;
}

void PointCloudPostProcessor::setVerticalInterpolationFactor(std::uint32_t factor) {
  const auto normalized = normalizedInterpolationFactor(factor);
  if (normalized == vertical_interpolation_factor_) {
    return;
  }
  vertical_interpolation_factor_ = normalized;
  rebuild();
}

std::uint32_t PointCloudPostProcessor::verticalInterpolationFactor() const {
  return vertical_interpolation_factor_;
}

bool PointCloudPostProcessor::push(std::shared_ptr<const PointCloudSnapshot> snapshot) {
  if (!snapshot || !snapshot->complete || snapshot->width == 0U || snapshot->height == 0U ||
      snapshot->points.size() != static_cast<std::size_t>(snapshot->width) * snapshot->height) {
    return false;
  }
  if (!history_.empty()) {
    const auto& previous = *history_.back();
    const bool new_session = snapshot->scan_frame_index <= previous.scan_frame_index;
    const bool geometry_changed = snapshot->width != previous.width ||
        snapshot->height != previous.height ||
        snapshot->processing_config_revision != previous.processing_config_revision;
    if (new_session || geometry_changed) {
      reset();
    }
  }
  history_.push_back(std::move(snapshot));
  while (history_.size() > history_frame_count_) {
    history_.pop_front();
  }
  rebuild();
  return true;
}

void PointCloudPostProcessor::reset() {
  history_.clear();
  display_frame_ = {};
}

const PointCloudDisplayFrame& PointCloudPostProcessor::displayFrame() const {
  return display_frame_;
}

void PointCloudPostProcessor::rebuild() {
  display_frame_ = {};
  if (history_.empty()) {
    return;
  }
  const auto& latest = *history_.back();
  display_frame_.scan_frame_index = latest.scan_frame_index;
  display_frame_.source_width = latest.width;
  display_frame_.source_height = latest.height;
  display_frame_.display_width = latest.width;
  display_frame_.source_valid_point_count = static_cast<std::size_t>(std::count_if(
      latest.points.begin(), latest.points.end(), finitePoint));

  const auto source_point_count = static_cast<std::size_t>(latest.width) * latest.height;
  std::vector<PointCloudDisplayPoint> fused(source_point_count);
  for (std::size_t index = 0U; index < source_point_count; ++index) {
    fused[index] = fuseCell(history_, index);
    if (fused[index].point.valid) {
      ++display_frame_.fused_point_count;
    }
  }

  const auto factor = latest.height > 1U ? vertical_interpolation_factor_ : 1U;
  display_frame_.display_height = (latest.height - 1U) * factor + 1U;
  display_frame_.points.assign(
      static_cast<std::size_t>(display_frame_.display_width) * display_frame_.display_height, {});

  for (std::uint32_t source_y = 0U; source_y < latest.height; ++source_y) {
    const auto display_y = source_y * factor;
    for (std::uint32_t x = 0U; x < latest.width; ++x) {
      display_frame_.points[static_cast<std::size_t>(display_y) * latest.width + x] =
          fused[static_cast<std::size_t>(source_y) * latest.width + x];
    }
    if (source_y + 1U >= latest.height || factor == 1U) {
      continue;
    }
    for (std::uint32_t step = 1U; step < factor; ++step) {
      const auto fraction = static_cast<float>(step) / static_cast<float>(factor);
      const auto interpolated_y = display_y + step;
      for (std::uint32_t x = 0U; x < latest.width; ++x) {
        const auto first_index = static_cast<std::size_t>(source_y) * latest.width + x;
        const auto second_index = first_index + latest.width;
        auto point = interpolate(fused[first_index], fused[second_index], fraction);
        if (point.point.valid) {
          ++display_frame_.interpolated_point_count;
        }
        display_frame_.points[
            static_cast<std::size_t>(interpolated_y) * latest.width + x] = point;
      }
    }
  }
}

}  // namespace fmcw
