#include "drivers/edfa/edfa_protocol.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fmcw {
namespace {

std::uint8_t checksum(const std::vector<std::uint8_t>& bytes) {
  std::uint32_t sum = 0;
  for (const auto byte : bytes) {
    sum += byte;
  }
  return static_cast<std::uint8_t>(sum & 0xFFU);
}

std::uint16_t bigEndian(const std::vector<std::uint8_t>& data, std::size_t offset) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8U) | data[offset + 1U]);
}

std::vector<std::uint8_t> twoByteValue(std::uint16_t value) {
  return {static_cast<std::uint8_t>(value >> 8U), static_cast<std::uint8_t>(value & 0xFFU)};
}

}  // namespace

std::vector<std::uint8_t> EdfaProtocol::query(std::uint8_t address) {
  std::vector<std::uint8_t> packet{0xEF, 0xEF, 0x02, address};
  packet.push_back(checksum(packet));
  return packet;
}

std::vector<std::uint8_t> EdfaProtocol::write(std::uint8_t address, const std::vector<std::uint8_t>& data) {
  if (data.size() > 253U) {
    return {};
  }
  std::vector<std::uint8_t> packet{0xEF, 0xEF, static_cast<std::uint8_t>(data.size() + 2U), address};
  packet.insert(packet.end(), data.begin(), data.end());
  packet.push_back(checksum(packet));
  return packet;
}

bool EdfaProtocol::parseResponse(const std::vector<std::uint8_t>& bytes, EdfaPacket& packet, std::string& error) {
  packet = {};
  if (bytes.size() < 5U || bytes[0] != 0xED || bytes[1] != 0xFA) {
    error = "EDFA response header is invalid";
    return false;
  }
  const auto length = static_cast<std::size_t>(bytes[2]);
  if (length < 2U || bytes.size() != length + 3U) {
    error = "EDFA response length is invalid";
    return false;
  }
  std::vector<std::uint8_t> without_sum(bytes.begin(), bytes.end() - 1);
  if (checksum(without_sum) != bytes.back()) {
    error = "EDFA response checksum mismatch";
    return false;
  }
  packet.address = bytes[3];
  packet.data.assign(bytes.begin() + 4, bytes.end() - 1);
  error.clear();
  return true;
}

std::vector<std::uint8_t> EdfaProtocol::queryStatus() { return query(0x00); }

std::vector<std::uint8_t> EdfaProtocol::queryTargetPower() { return query(0x03); }

std::vector<std::uint8_t> EdfaProtocol::queryMode() { return query(0x05); }

std::vector<std::uint8_t> EdfaProtocol::queryActivation() { return query(0x25); }

std::vector<std::uint8_t> EdfaProtocol::setMode(EdfaControlMode mode) {
  std::uint8_t value = 0;
  switch (mode) {
    case EdfaControlMode::Apc: value = 0; break;
    case EdfaControlMode::Acc: value = 1; break;
    case EdfaControlMode::Agc: value = 2; break;
  }
  return write(0x06, {value});
}

std::vector<std::uint8_t> EdfaProtocol::setTargetPowerDbm(double dbm) {
  const auto encoded = std::llround((dbm + 70.0) * 100.0);
  if (encoded < 0 || encoded > std::numeric_limits<std::uint16_t>::max()) {
    return {};
  }
  return write(0x04, twoByteValue(static_cast<std::uint16_t>(encoded)));
}

std::vector<std::uint8_t> EdfaProtocol::setActivation(bool enabled) {
  return write(0x26, {static_cast<std::uint8_t>(enabled ? 1 : 0)});
}

bool EdfaProtocol::decodeStatus(const EdfaPacket& packet, EdfaDeviceReading& reading, std::string& error) {
  if (packet.address != 0x00 || packet.data.size() != 12U) {
    error = "EDFA status response must contain 12 data bytes at address 0x00";
    return false;
  }
  reading.current_ma = bigEndian(packet.data, 0);
  reading.input_power_dbm = static_cast<double>(bigEndian(packet.data, 4)) / 100.0 - 70.0;
  reading.output_power_dbm = static_cast<double>(bigEndian(packet.data, 6)) / 100.0 - 70.0;
  error.clear();
  return true;
}

bool EdfaProtocol::decodePowerDbm(const EdfaPacket& packet, double& dbm, std::string& error) {
  if (packet.address != 0x03 || packet.data.size() != 2U) {
    error = "EDFA target power response is invalid";
    return false;
  }
  dbm = static_cast<double>(bigEndian(packet.data, 0)) / 100.0 - 70.0;
  error.clear();
  return true;
}

bool EdfaProtocol::decodeMode(const EdfaPacket& packet, EdfaControlMode& mode, std::string& error) {
  if (packet.address != 0x05 || packet.data.size() != 1U || packet.data[0] > 2U) {
    error = "EDFA working mode response is invalid";
    return false;
  }
  mode = packet.data[0] == 0 ? EdfaControlMode::Apc :
         packet.data[0] == 1 ? EdfaControlMode::Acc : EdfaControlMode::Agc;
  error.clear();
  return true;
}

bool EdfaProtocol::decodeActivation(const EdfaPacket& packet, bool& enabled, std::string& error) {
  if (packet.address != 0x25 || packet.data.size() != 1U || packet.data[0] > 1U) {
    error = "EDFA activation response is invalid";
    return false;
  }
  enabled = packet.data[0] == 1U;
  error.clear();
  return true;
}

}  // namespace fmcw
