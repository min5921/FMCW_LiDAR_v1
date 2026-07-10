#include "drivers/mcu/mcu_protocol.h"

#include <limits>
#include <sstream>

namespace fmcw {

std::string McuProtocol::clearCommand() { return "CLR\n"; }

std::string McuProtocol::dataCommand(const McuWaveformFrame& frame) {
  std::ostringstream command;
  command << "DATA," << frame.a << ',' << frame.b << ',' << frame.c << ',' << frame.d << ','
          << (frame.trigger ? 1 : 0) << '\n';
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
