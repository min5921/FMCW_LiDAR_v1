#include "core/scan_trajectory.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fmcw {
namespace {

constexpr double kCommandRangeEpsilon = 1.0e-9;

float indexToAngle(std::uint32_t index, std::uint32_t count,
                   double minimum_angle_deg, double maximum_angle_deg) {
  if (count < 2U || index >= count || !std::isfinite(minimum_angle_deg) ||
      !std::isfinite(maximum_angle_deg)) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  const auto fraction = static_cast<double>(index) / static_cast<double>(count - 1U);
  return static_cast<float>(minimum_angle_deg +
      (maximum_angle_deg - minimum_angle_deg) * fraction);
}

float lineIndexToElevation(std::uint32_t index, std::uint32_t count,
                           double minimum_angle_deg, double maximum_angle_deg) {
  // DMA B-scan lines arrive from the top of the vector scan to the bottom.
  return indexToAngle(index, count, maximum_angle_deg, minimum_angle_deg);
}

std::size_t trajectoryIndexForRecord(const McuWaveformSnapshot& waveform,
                                     std::uint32_t marker_index,
                                     std::uint32_t record_index,
                                     double trigger_rate_hz) {
  const auto elapsed_samples = static_cast<std::uint64_t>(std::floor(
      static_cast<double>(record_index) * waveform.sample_rate_hz / trigger_rate_hz));
  return static_cast<std::size_t>((static_cast<std::uint64_t>(marker_index) + elapsed_samples) %
      waveform.frames.size());
}

}  // namespace

bool prepareTrajectoryLine(const SystemConfig& config,
                           const McuWaveformSnapshot& waveform,
                           std::uint64_t line_sequence,
                           std::uint32_t record_count,
                           TrajectoryLineContext& context) {
  context = {};
  if (!waveform.valid() || record_count == 0U || !(config.laser.sweep_rate_hz > 0.0)) {
    return false;
  }
  const auto& marker_indices = waveform.emitted_marker_indices.empty()
      ? waveform.logical_marker_indices
      : waveform.emitted_marker_indices;
  context.y_index = static_cast<std::uint32_t>(line_sequence % marker_indices.size());
  context.marker_index = marker_indices[context.y_index];
  context.record_count = record_count;
  const bool reverse_line = config.scan.bidirectional && (context.y_index % 2U) != 0U;
  context.direction = reverse_line ? ScanDirection::Decreasing : ScanDirection::Increasing;

  if (record_count > 1U) {
    const auto& first = waveform.frames[trajectoryIndexForRecord(
        waveform, context.marker_index, 0U, config.laser.sweep_rate_hz)];
    const auto& last = waveform.frames[trajectoryIndexForRecord(
        waveform, context.marker_index, record_count - 1U, config.laser.sweep_rate_hz)];
    const auto x_delta = std::abs(last.command_x - first.command_x);
    const auto y_delta = std::abs(last.command_y - first.command_y);
    if (std::max(x_delta, y_delta) > static_cast<float>(kCommandRangeEpsilon)) {
      context.fast_axis = x_delta >= y_delta ? ScanAxis::X : ScanAxis::Y;
    }
  }
  context.valid = true;
  return true;
}

bool mapTrajectoryRecord(const SystemConfig& config,
                         const McuWaveformSnapshot& waveform,
                         const TrajectoryLineContext& context,
                         std::uint32_t record_index,
                         ScanPosition& position) {
  if (!context.valid || !waveform.valid() || record_index >= context.record_count) {
    return false;
  }
  const auto trajectory_index = trajectoryIndexForRecord(
      waveform, context.marker_index, record_index, config.laser.sweep_rate_hz);
  const auto& command = waveform.frames[trajectory_index];

  position = {};
  position.x_index = context.direction == ScanDirection::Decreasing
      ? context.record_count - 1U - record_index
      : record_index;
  position.y_index = context.y_index;
  position.trajectory_sample_index = static_cast<std::uint32_t>(trajectory_index);
  position.x_command = command.command_x;
  position.y_command = command.command_y;
  // The operator's bidirectional setting controls odd-line reversal. Source
  // commands remain provenance and never reverse A-scan samples or FFT input.
  position.x_angle_deg = indexToAngle(position.x_index, context.record_count,
      config.scan.x_start_deg, config.scan.x_end_deg);
  position.y_angle_deg = lineIndexToElevation(position.y_index, config.scan.y_line_count,
      config.scan.y_start_deg, config.scan.y_end_deg);
  position.fast_axis = context.fast_axis;
  position.fast_axis_direction = context.direction;
  position.source = ScanCoordinateSource::McuTrajectory;
  position.angle_calibrated = std::isfinite(position.x_angle_deg) &&
      std::isfinite(position.y_angle_deg);
  position.valid = position.angle_calibrated;
  return position.valid;
}

bool alignReplayRasterPosition(const SystemConfig& config, ScanPosition& position) {
  const auto azimuth_deg = indexToAngle(position.x_index, config.scan.x_pixel_count,
      config.scan.x_start_deg, config.scan.x_end_deg);
  const auto elevation_deg = lineIndexToElevation(position.y_index, config.scan.y_line_count,
      config.scan.y_start_deg, config.scan.y_end_deg);
  if (!std::isfinite(azimuth_deg) || !std::isfinite(elevation_deg)) {
    return false;
  }

  // Raw trajectory commands remain provenance. Replay uses the currently
  // applied raster geometry so B-scan columns and Cartesian angles cannot
  // diverge because an older recording stored command-derived angles.
  position.x_angle_deg = azimuth_deg;
  position.y_angle_deg = elevation_deg;
  position.source = ScanCoordinateSource::Replay;
  position.angle_calibrated = true;
  position.valid = true;
  return true;
}

void stampGeneratedRasterPosition(const SystemConfig& config, RawFrame& frame) {
  const auto zero_based = frame.metadata.frame_id == 0 ? 0 : frame.metadata.frame_id - 1;
  const auto y = static_cast<std::uint32_t>(
      (zero_based / config.scan.x_pixel_count) % config.scan.y_line_count);
  const auto step = static_cast<std::uint32_t>(zero_based % config.scan.x_pixel_count);
  const bool decreasing = config.scan.bidirectional && (y % 2U) != 0U;
  const auto x = decreasing ? config.scan.x_pixel_count - 1U - step : step;
  auto& position = frame.metadata.scan_position;
  position = {};
  position.x_index = x;
  position.y_index = y;
  position.trajectory_sample_index = static_cast<std::uint32_t>(zero_based);
  position.x_command = static_cast<float>(-1.0 +
      2.0 * static_cast<double>(x) / static_cast<double>(config.scan.x_pixel_count - 1U));
  position.y_command = static_cast<float>(1.0 -
      2.0 * static_cast<double>(y) / static_cast<double>(config.scan.y_line_count - 1U));
  position.x_angle_deg = indexToAngle(x, config.scan.x_pixel_count,
      config.scan.x_start_deg, config.scan.x_end_deg);
  position.y_angle_deg = lineIndexToElevation(y, config.scan.y_line_count,
      config.scan.y_start_deg, config.scan.y_end_deg);
  position.fast_axis = ScanAxis::X;
  position.fast_axis_direction = decreasing
      ? ScanDirection::Decreasing
      : ScanDirection::Increasing;
  position.source = ScanCoordinateSource::GeneratedRaster;
  position.angle_calibrated = true;
  position.valid = true;
}

}  // namespace fmcw
