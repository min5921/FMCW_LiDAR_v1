#pragma once

#include "core/config_types.h"
#include "core/device_interfaces.h"

#include <cstdint>

namespace fmcw {

struct TrajectoryLineContext {
  std::uint32_t y_index = 0;
  std::uint32_t marker_index = 0;
  std::uint32_t record_count = 0;
  ScanAxis fast_axis = ScanAxis::Unknown;
  ScanDirection direction = ScanDirection::Unknown;
  bool valid = false;
};

bool prepareTrajectoryLine(const SystemConfig& config,
                           const McuWaveformSnapshot& waveform,
                           std::uint64_t line_sequence,
                           std::uint32_t record_count,
                           TrajectoryLineContext& context);

bool mapTrajectoryRecord(const SystemConfig& config,
                         const McuWaveformSnapshot& waveform,
                         const TrajectoryLineContext& context,
                         std::uint32_t record_index,
                         ScanPosition& position);

bool alignReplayRasterPosition(const SystemConfig& config, ScanPosition& position);

void stampGeneratedRasterPosition(const SystemConfig& config, RawFrame& frame);

}  // namespace fmcw
