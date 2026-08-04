#pragma once

#include "core/device_interfaces.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fmcw {

inline constexpr double kMcuWaveformPointRateHz = 100000.0;
inline constexpr std::uint32_t kMcuWaveformMaximumPoints = 15000U;

struct McuWaveformInfo {
  McuWaveformSource source = McuWaveformSource::GeneratedRaster;
  std::uint32_t source_point_count = 0;
  std::uint32_t output_point_count = 0;
  std::uint32_t marker_rising_edges = 0;
  std::int32_t trigger_shift_samples = 0;
  double source_sample_rate_hz = 0.0;
  double output_sample_rate_hz = kMcuWaveformPointRateHz;
  float minimum_x_command = 0.0F;
  float maximum_x_command = 0.0F;
  float minimum_y_command = 0.0F;
  float maximum_y_command = 0.0F;
  bool resampled = false;
};

struct McuResponse {
  bool acknowledged = false;
  bool error = false;
  std::string code;
  std::string detail;
  std::uint32_t count = 0;
  bool has_count = false;
};

class McuProtocol {
 public:
  static std::vector<McuWaveformFrame> buildConfiguredWaveform(const SystemConfig& config,
                                                                McuWaveformInfo& info,
                                                                std::string& error);
  static std::vector<McuWaveformFrame> buildFullFrameWaveform(const SystemConfig& config,
                                                               std::string& error);
  static std::vector<McuWaveformFrame> loadLegacyXymWaveform(std::string_view path,
                                                             McuWaveformInfo& info,
                                                             std::string& error);
  static McuWaveformSnapshotPtr snapshotForUploadedWaveform(
      const std::vector<McuWaveformFrame>& frames, double sample_rate_hz);
  static std::string clearCommand();
  static std::string dataCommand(const McuWaveformFrame& frame);
  static std::string loadDoneCommand();
  static std::string startCommand();
  static std::string stopCommand();
  static McuResponse parseResponse(std::string_view line);
};

}  // namespace fmcw
