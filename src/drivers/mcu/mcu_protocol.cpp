#include "drivers/mcu/mcu_protocol.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

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

}  // namespace

std::vector<McuWaveformFrame> McuProtocol::buildFullFrameWaveform(const SystemConfig& config,
                                                                  std::string& error) {
  const auto x_count = derivedAScanCount(config);
  const auto y_count = config.scan.y_line_count;
  const auto point_count = derivedFramePointCount(config);
  if (x_count < 2U || y_count < 2U || point_count > 15000U) {
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
      frames.push_back(frame);
    }
  }
  error.clear();
  return frames;
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
