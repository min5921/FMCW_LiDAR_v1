#include "drivers/mcu/mcu_protocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace fmcw {
namespace {

std::uint16_t mirrorCode(double normalized) {
  constexpr double kBiasVolts = 90.0;
  constexpr double kDifferentialVolts = 120.0;
  constexpr double kFullScaleVolts = 200.0;
  const auto voltage = std::clamp(kBiasVolts + std::clamp(normalized, -1.0, 1.0) *
      kDifferentialVolts / 2.0, 0.0, kFullScaleVolts);
  return static_cast<std::uint16_t>(std::llround(voltage / kFullScaleVolts * 65535.0));
}

struct XymPoint {
  double x = 0.0;
  double y = 0.0;
  double marker = 0.0;
};

bool blankLine(const std::string& line) {
  return std::all_of(line.begin(), line.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
}

bool parseLegacyXym(std::istream& input, double& sample_rate_hz,
                    std::vector<XymPoint>& points, std::string& error) {
  std::string line;
  std::uint32_t line_number = 0;
  bool header_read = false;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty() || blankLine(line)) {
      continue;
    }
    if (!header_read) {
      std::istringstream header(line);
      std::string key;
      if (!(header >> key >> sample_rate_hz)) {
        error = "Waveform line 1 must use the format 'sps <sample-rate>'";
        return false;
      }
      if (key.size() >= 3U && static_cast<unsigned char>(key[0]) == 0xEFU &&
          static_cast<unsigned char>(key[1]) == 0xBBU &&
          static_cast<unsigned char>(key[2]) == 0xBFU) {
        key.erase(0, 3);
      }
      std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
      });
      if (key != "sps" || !std::isfinite(sample_rate_hz) || !(sample_rate_hz > 0.0)) {
        error = "Waveform header must contain a positive 'sps <sample-rate>' value";
        return false;
      }
      header_read = true;
      continue;
    }

    XymPoint point;
    std::istringstream row(line);
    if (!(row >> point.x >> point.y >> point.marker) ||
        !std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(point.marker)) {
      error = "Waveform line " + std::to_string(line_number) +
          " must contain finite X, Y, and M values";
      return false;
    }
    points.push_back(point);
  }

  if (!header_read) {
    error = "Legacy X/Y/M waveform file is empty";
    return false;
  }
  if (points.size() < 2U) {
    error = "Legacy X/Y/M waveform must contain at least two data points";
    return false;
  }
  if (points.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "Legacy X/Y/M waveform contains too many source points";
    return false;
  }
  return true;
}

std::vector<XymPoint> resampleLegacyXym(const std::vector<XymPoint>& source,
                                        double source_rate_hz,
                                        std::string& error) {
  if (std::abs(source_rate_hz - kMcuWaveformPointRateHz) < 1.0e-9) {
    return source;
  }

  const double requested_count = std::nearbyint(
      static_cast<double>(source.size()) * kMcuWaveformPointRateHz / source_rate_hz);
  if (!std::isfinite(requested_count) || requested_count < 2.0 ||
      requested_count > static_cast<double>(kMcuWaveformMaximumPoints)) {
    error = "Resampled waveform must contain 2..15000 points at the fixed 100 kHz MCU rate";
    return {};
  }

  const auto output_count = static_cast<std::size_t>(requested_count);
  std::vector<XymPoint> output;
  output.reserve(output_count);
  for (std::size_t index = 0; index < output_count; ++index) {
    const double source_position = static_cast<double>(index) * source_rate_hz /
        kMcuWaveformPointRateHz;
    const auto lower = static_cast<std::size_t>(std::floor(source_position));
    XymPoint point;
    if (lower >= source.size() - 1U) {
      point = source.back();
    } else {
      const double fraction = source_position - static_cast<double>(lower);
      point.x = source[lower].x + (source[lower + 1U].x - source[lower].x) * fraction;
      point.y = source[lower].y + (source[lower + 1U].y - source[lower].y) * fraction;
      const auto marker_index = std::min(
          static_cast<std::size_t>(std::nearbyint(source_position)), source.size() - 1U);
      point.marker = source[marker_index].marker;
    }
    output.push_back(point);
  }
  return output;
}

std::uint32_t markerRisingEdges(const std::vector<McuWaveformFrame>& frames) {
  std::uint32_t edges = 0;
  bool previous = frames.empty() ? false : frames.back().trigger;
  for (const auto& frame : frames) {
    if (frame.trigger && !previous) {
      ++edges;
    }
    previous = frame.trigger;
  }
  return edges;
}

template <typename Marker>
std::vector<std::uint32_t> markerRisingIndices(const std::vector<McuWaveformFrame>& frames,
                                                Marker marker) {
  std::vector<std::uint32_t> indices;
  bool previous = frames.empty() ? false : marker(frames.back());
  for (std::size_t index = 0; index < frames.size(); ++index) {
    const bool current = marker(frames[index]);
    if (current && !previous) {
      indices.push_back(static_cast<std::uint32_t>(index));
    }
    previous = current;
  }
  return indices;
}

void describeCommandRange(const std::vector<McuWaveformFrame>& frames,
                          float& minimum_x, float& maximum_x,
                          float& minimum_y, float& maximum_y) {
  if (frames.empty()) {
    minimum_x = maximum_x = minimum_y = maximum_y = 0.0F;
    return;
  }
  minimum_x = maximum_x = frames.front().command_x;
  minimum_y = maximum_y = frames.front().command_y;
  for (const auto& frame : frames) {
    minimum_x = std::min(minimum_x, frame.command_x);
    maximum_x = std::max(maximum_x, frame.command_x);
    minimum_y = std::min(minimum_y, frame.command_y);
    maximum_y = std::max(maximum_y, frame.command_y);
  }
}

std::int32_t shiftTriggerMarkers(std::vector<McuWaveformFrame>& frames,
                                 std::int32_t requested_shift) {
  if (frames.empty() || requested_shift == 0) {
    return 0;
  }

  const auto period = static_cast<std::int64_t>(frames.size());
  const auto shift = static_cast<std::int64_t>(requested_shift) % period;
  if (shift == 0) {
    return 0;
  }

  std::vector<std::uint8_t> shifted_markers(frames.size(), 0U);
  for (std::size_t source_index = 0; source_index < frames.size(); ++source_index) {
    auto destination_index = (static_cast<std::int64_t>(source_index) + shift) % period;
    if (destination_index < 0) {
      destination_index += period;
    }
    shifted_markers[static_cast<std::size_t>(destination_index)] =
        frames[source_index].trigger ? 1U : 0U;
  }
  for (std::size_t index = 0; index < frames.size(); ++index) {
    frames[index].trigger = shifted_markers[index] != 0U;
  }
  return static_cast<std::int32_t>(shift);
}

std::vector<McuWaveformFrame> convertLegacyXym(std::istream& input,
                                               McuWaveformInfo& info,
                                               std::string& error) {
  info = {};
  info.source = McuWaveformSource::LegacyXymFile;
  double source_rate_hz = 0.0;
  std::vector<XymPoint> source;
  if (!parseLegacyXym(input, source_rate_hz, source, error)) {
    return {};
  }
  auto converted = resampleLegacyXym(source, source_rate_hz, error);
  if (converted.empty()) {
    return {};
  }
  if (converted.size() > kMcuWaveformMaximumPoints) {
    error = "Legacy X/Y/M waveform exceeds the firmware 15000-point buffer";
    return {};
  }

  std::vector<McuWaveformFrame> frames;
  frames.reserve(converted.size());
  for (const auto& point : converted) {
    McuWaveformFrame frame;
    frame.a = mirrorCode(point.x);
    frame.b = mirrorCode(-point.x);
    frame.c = mirrorCode(point.y);
    frame.d = mirrorCode(-point.y);
    frame.trigger = point.marker >= 0.5;
    frame.command_x = static_cast<float>(point.x);
    frame.command_y = static_cast<float>(point.y);
    frame.logical_trigger = frame.trigger;
    frames.push_back(frame);
  }

  info.source_point_count = static_cast<std::uint32_t>(source.size());
  info.output_point_count = static_cast<std::uint32_t>(frames.size());
  info.marker_rising_edges = markerRisingEdges(frames);
  info.source_sample_rate_hz = source_rate_hz;
  info.output_sample_rate_hz = kMcuWaveformPointRateHz;
  info.resampled = std::abs(source_rate_hz - kMcuWaveformPointRateHz) >= 1.0e-9;
  describeCommandRange(frames, info.minimum_x_command, info.maximum_x_command,
                       info.minimum_y_command, info.maximum_y_command);
  error.clear();
  return frames;
}

void finalizeConfiguredWaveform(const SystemConfig& config,
                                std::vector<McuWaveformFrame>& frames,
                                McuWaveformInfo& info) {
  info.trigger_shift_samples = shiftTriggerMarkers(
      frames, config.scan.trigger_shift_samples);
  info.marker_rising_edges = markerRisingEdges(frames);
  describeCommandRange(frames, info.minimum_x_command, info.maximum_x_command,
                       info.minimum_y_command, info.maximum_y_command);
}

}  // namespace

std::vector<McuWaveformFrame> McuProtocol::buildConfiguredWaveform(const SystemConfig& config,
                                                                   McuWaveformInfo& info,
                                                                   std::string& error) {
  std::vector<McuWaveformFrame> frames;
  if (config.mcu.waveform_source == McuWaveformSource::LegacyXymFile) {
    frames = loadLegacyXymWaveform(config.mcu.waveform_file, info, error);
  } else {
    frames = buildFullFrameWaveform(config, error);
    if (!frames.empty()) {
      info = {};
      info.source = McuWaveformSource::GeneratedRaster;
      info.source_point_count = static_cast<std::uint32_t>(frames.size());
      info.output_point_count = static_cast<std::uint32_t>(frames.size());
      info.source_sample_rate_hz = kMcuWaveformPointRateHz;
      info.output_sample_rate_hz = kMcuWaveformPointRateHz;
    }
  }

  if (frames.empty()) {
    return {};
  }
  finalizeConfiguredWaveform(config, frames, info);
  error.clear();
  return frames;
}

std::vector<McuWaveformFrame> McuProtocol::buildConfiguredLegacyXymWaveform(
    const SystemConfig& config, std::string_view contents,
    McuWaveformInfo& info, std::string& error) {
  if (config.mcu.waveform_source != McuWaveformSource::LegacyXymFile) {
    error = "Legacy X/Y/M contents require the legacy waveform source mode";
    return {};
  }
  auto frames = loadLegacyXymWaveformContents(contents, info, error);
  if (frames.empty()) {
    return {};
  }
  finalizeConfiguredWaveform(config, frames, info);
  error.clear();
  return frames;
}

std::vector<McuWaveformFrame> McuProtocol::buildFullFrameWaveform(const SystemConfig& config,
                                                                  std::string& error) {
  const auto x_count = derivedAScanCount(config);
  const auto y_count = config.scan.y_line_count;
  const auto point_count = derivedFramePointCount(config);
  if (x_count < 2U || y_count < 2U || point_count > kMcuWaveformMaximumPoints) {
    error = "Full-frame waveform must contain at least 2 x 2 points and fit the firmware 15000-point buffer";
    return {};
  }

  std::vector<McuWaveformFrame> frames;
  frames.reserve(static_cast<std::size_t>(point_count));
  for (std::uint32_t y = 0; y < y_count; ++y) {
    const auto y_normalized = -1.0 + 2.0 * static_cast<double>(y) /
        static_cast<double>(y_count - 1U);
    for (std::uint32_t step = 0; step < x_count; ++step) {
      const auto x_index = config.scan.bidirectional && (y % 2U) != 0U
          ? x_count - 1U - step
          : step;
      const auto x_normalized = -1.0 + 2.0 * static_cast<double>(x_index) /
          static_cast<double>(x_count - 1U);
      McuWaveformFrame frame;
      frame.a = mirrorCode(x_normalized);
      frame.b = mirrorCode(-x_normalized);
      frame.c = mirrorCode(y_normalized);
      frame.d = mirrorCode(-y_normalized);
      frame.trigger = step == 0U;
      frame.command_x = static_cast<float>(x_normalized);
      frame.command_y = static_cast<float>(y_normalized);
      frame.logical_trigger = frame.trigger;
      frames.push_back(frame);
    }
  }
  error.clear();
  return frames;
}

std::vector<McuWaveformFrame> McuProtocol::loadLegacyXymWaveform(std::string_view path,
                                                                 McuWaveformInfo& info,
                                                                 std::string& error) {
  if (path.empty()) {
    error = "Select a legacy X/Y/M waveform file before upload";
    return {};
  }
  std::ifstream input{std::string(path), std::ios::binary};
  if (!input.is_open()) {
    error = "Unable to open legacy X/Y/M waveform file: " + std::string(path);
    return {};
  }
  return convertLegacyXym(input, info, error);
}

std::vector<McuWaveformFrame> McuProtocol::loadLegacyXymWaveformContents(
    std::string_view contents, McuWaveformInfo& info, std::string& error) {
  if (contents.empty()) {
    error = "Legacy X/Y/M waveform contents are empty";
    return {};
  }
  std::istringstream input{std::string(contents)};
  return convertLegacyXym(input, info, error);
}

McuWaveformSnapshotPtr McuProtocol::snapshotForUploadedWaveform(
    const std::vector<McuWaveformFrame>& frames, double sample_rate_hz) {
  if (frames.empty() || !(sample_rate_hz > 0.0) || !std::isfinite(sample_rate_hz)) {
    return {};
  }
  auto snapshot = std::make_shared<McuWaveformSnapshot>();
  snapshot->frames = frames;
  snapshot->sample_rate_hz = sample_rate_hz;
  snapshot->logical_marker_indices = markerRisingIndices(
      frames, [](const McuWaveformFrame& frame) { return frame.logical_trigger; });
  snapshot->emitted_marker_indices = markerRisingIndices(
      frames, [](const McuWaveformFrame& frame) { return frame.trigger; });
  if (snapshot->logical_marker_indices.empty()) {
    snapshot->logical_marker_indices = snapshot->emitted_marker_indices;
  }
  describeCommandRange(frames, snapshot->minimum_x_command, snapshot->maximum_x_command,
                       snapshot->minimum_y_command, snapshot->maximum_y_command);
  return snapshot->valid() ? McuWaveformSnapshotPtr(std::move(snapshot)) : McuWaveformSnapshotPtr{};
}

std::string McuProtocol::clearCommand() { return "CLR\n"; }

std::string McuProtocol::dataCommand(const McuWaveformFrame& frame) {
  std::ostringstream command;
  command << "DATA," << frame.a << ',' << frame.b << ',' << frame.c << ',' << frame.d << ','
          << (frame.trigger ? 255 : 0) << '\n';
  return command.str();
}

std::string McuProtocol::loadDoneCommand() { return "LOAD_DONE\n"; }

std::string McuProtocol::startCommand() { return "START\n"; }

std::string McuProtocol::stopCommand() { return "STOP\n"; }

McuResponse McuProtocol::parseResponse(std::string_view line) {
  McuResponse response;
  if (line.rfind("ACK:", 0) == 0) {
    response.acknowledged = true;
    const auto comma = line.find(',');
    response.code = std::string(line.substr(4, comma == std::string_view::npos ? line.size() - 4 : comma - 4));
    if (comma != std::string_view::npos && comma + 1 < line.size()) {
      try {
        const auto text = std::string(line.substr(comma + 1));
        std::size_t consumed = 0;
        const auto count = std::stoull(text, &consumed, 10);
        if (consumed == text.size() && count <= std::numeric_limits<std::uint32_t>::max()) {
          response.count = static_cast<std::uint32_t>(count);
          response.has_count = true;
        }
      } catch (...) {
        response.acknowledged = false;
        response.detail = "Malformed ACK count";
      }
    }
    return response;
  }
  if (line.rfind("ERR:", 0) == 0) {
    response.error = true;
    response.code = std::string(line.substr(4));
    response.detail = std::string(line);
    return response;
  }
  response.detail = "Unknown MCU response: " + std::string(line);
  return response;
}

}  // namespace fmcw
