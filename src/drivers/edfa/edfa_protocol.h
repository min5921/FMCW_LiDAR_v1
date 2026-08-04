#pragma once

#include "core/config_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fmcw {

struct EdfaPacket {
  std::uint8_t address = 0;
  std::vector<std::uint8_t> data;
};

struct EdfaDeviceReading {
  double current_ma = 0.0;
  double input_power_dbm = 0.0;
  double output_power_dbm = 0.0;
};

class EdfaProtocol {
 public:
  static std::vector<std::uint8_t> query(std::uint8_t address);
  static std::vector<std::uint8_t> write(std::uint8_t address, const std::vector<std::uint8_t>& data);
  static bool parseResponse(const std::vector<std::uint8_t>& bytes, EdfaPacket& packet, std::string& error);

  static std::vector<std::uint8_t> queryStatus();
  static std::vector<std::uint8_t> queryTargetPower();
  static std::vector<std::uint8_t> queryMode();
  static std::vector<std::uint8_t> queryActivation();
  static std::vector<std::uint8_t> setMode(EdfaControlMode mode);
  static std::vector<std::uint8_t> setTargetPowerDbm(double dbm);
  static std::vector<std::uint8_t> setActivation(bool enabled);

  static bool decodeStatus(const EdfaPacket& packet, EdfaDeviceReading& reading, std::string& error);
  static bool decodePowerDbm(const EdfaPacket& packet, double& dbm, std::string& error);
  static bool decodeMode(const EdfaPacket& packet, EdfaControlMode& mode, std::string& error);
  static bool decodeActivation(const EdfaPacket& packet, bool& enabled, std::string& error);
};

}  // namespace fmcw
