#pragma once

#include "core/config_types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fmcw {

struct SerialSettings {
  std::string port;
  std::uint32_t baud_rate = 115200;
  SerialParity parity = SerialParity::None;
  std::uint32_t stop_bits = 1;
};

class ISerialTransport {
 public:
  virtual ~ISerialTransport() = default;
  virtual bool open(const SerialSettings& settings, std::string& error) = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;
  virtual bool purge(std::string& error) = 0;
  virtual bool write(const std::vector<std::uint8_t>& data, std::string& error) = 0;
  virtual bool readExact(std::size_t byte_count, std::vector<std::uint8_t>& data,
                         std::chrono::milliseconds timeout, std::string& error) = 0;
  virtual bool readLine(std::string& line, std::chrono::milliseconds timeout, std::string& error) = 0;
};

class PlatformSerialTransport final : public ISerialTransport {
 public:
  PlatformSerialTransport();
  ~PlatformSerialTransport() override;

  PlatformSerialTransport(const PlatformSerialTransport&) = delete;
  PlatformSerialTransport& operator=(const PlatformSerialTransport&) = delete;

  bool open(const SerialSettings& settings, std::string& error) override;
  void close() override;
  bool isOpen() const override;
  bool purge(std::string& error) override;
  bool write(const std::vector<std::uint8_t>& data, std::string& error) override;
  bool readExact(std::size_t byte_count, std::vector<std::uint8_t>& data,
                 std::chrono::milliseconds timeout, std::string& error) override;
  bool readLine(std::string& line, std::chrono::milliseconds timeout, std::string& error) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::vector<std::uint8_t> bytesFromString(const std::string& text);
std::vector<std::string> availableSerialPorts();

}  // namespace fmcw
