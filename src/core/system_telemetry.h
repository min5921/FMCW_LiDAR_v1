#pragma once

#include "core/device_interfaces.h"

#include <cstdint>

namespace fmcw {

struct AcquisitionTelemetrySnapshot {
  std::uint64_t timestamp_ns = 0;
  std::uint64_t config_revision = 0;
  bool configured = false;
  bool connected = false;
  bool running = false;
  DigitizerTelemetry digitizer;
  EdfaStatus edfa;
  McuStatus mcu;
};

}  // namespace fmcw
