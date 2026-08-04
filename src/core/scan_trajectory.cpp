#include "core/scan_trajectory.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fmcw {
namespace {

constexpr double kCommandRangeEpsilon = 1.0e-9;

float commandToAngle(float command, float minimum_command, float maximum_command,
                     double minimum_angle_deg, double maximum_angle_deg) {
  const auto span = static_cast<double>(maximum_command) - minimum_command;
  if (!std::isfinite(command) || !std::isfinite(span) || std::abs(span) <= kCommandRangeEpsilon) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  const auto fraction = std::clamp(
      (static_cast<double>(command) - minimum_command) / span, 0.0, 1.0);
  return static_cast<float>(minimum_angle_deg +
      (maximum_angle_deg - minimum_angle_deg) * fraction);
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
  context.y_index = static_cast<std::uint32_t>(
      line_sequence % waveform.logical_marker_indices.size());
  context.marker_index = waveform.logical_marker_indices[context.y_index];
  context.record_count = record_count;

  if (record_count > 1U) {
    const auto& first = waveform.frames[trajectoryIndexForRecord(
        waveform, context.marker_index, 0U, config.laser.sweep_rate_hz)];
    const auto& last = waveform.frames[trajectoryIndexForRecord(
        waveform, context.marker_index, record_count - 1U, config.laser.sweep_rate_hz)];
    const auto x_delta = std::abs(last.command_x - first.command_x);
    const auto y_delta = std::abs(last.command_y - first.command_y);
    if (std::max(x_delta, y_delta) > static_cast<float>(kCommandRangeEpsilon)) {
      context.fast_axis = x_delta >= y_delta ? ScanAxis::X : ScanAxis::Y;
      const auto signed_delta = context.fast_axis == ScanAxis::X
          ? last.command_x - first.command_x
          : last.command_y - first.command_y;
      context.direction = signed_delta > 0.0F
          ? ScanDirection::Increasing
          : ScanDirection::Decreasing;
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
  position.x_angle_deg = commandToAngle(command.command_x,
      waveform.minimum_x_command, waveform.maximum_x_command,
      config.scan.x_start_deg, config.scan.x_end_deg);
  position.y_angle_deg = commandToAngle(command.command_y,
      waveform.minimum_y_command, waveform.maximum_y_command,
      config.scan.y_start_deg, config.scan.y_end_deg);
  position.fast_axis = context.fast_axis;
  position.fast_axis_direction = context.direction;
  position.source = ScanCoordinateSource::McuTrajectory;
  position.angle_calibrated = std::isfinite(position.x_angle_deg) &&
      std::isfinite(position.y_angle_deg);
  position.valid = position.angle_calibrated;
  return position.valid;
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
  position.y_command = static_cast<float>(-1.0 +
      2.0 * static_cast<double>(y) / static_cast<double>(config.scan.y_line_count - 1U));
  position.x_angle_deg = static_cast<float>(config.scan.x_start_deg +
      (config.scan.x_end_deg - config.scan.x_start_deg) * static_cast<double>(x) /
          static_cast<double>(config.scan.x_pixel_count - 1U));
  position.y_angle_deg = static_cast<float>(config.scan.y_start_deg +
      (config.scan.y_end_deg - config.scan.y_start_deg) * static_cast<double>(y) /
          static_cast<double>(config.scan.y_line_count - 1U));
  position.fast_axis = ScanAxis::X;
  position.fast_axis_direction = decreasing
      ? ScanDirection::Decreasing
      : ScanDirection::Increasing;
  position.source = ScanCoordinateSource::GeneratedRaster;
  position.angle_calibrated = true;
  position.valid = true;
}

}  // namespace fmcw
