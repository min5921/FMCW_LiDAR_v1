#pragma once

#include "core/device_interfaces.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace fmcw {

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
  static std::string clearCommand();
  static std::string dataCommand(const McuWaveformFrame& frame);
  static std::string loadDoneCommand();
  static std::string startCommand();
  static std::string stopCommand();
  static McuResponse parseResponse(std::string_view line);
};

}  // namespace fmcw
