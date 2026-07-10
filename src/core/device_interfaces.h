#pragma once

#include "core/frame_types.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace fmcw {

struct SystemConfig;

struct DeviceStatus {
  bool connected = false;
  bool ready = false;
  bool running = false;
  std::string detail;
};

enum class FrameWaitResult {
  FrameReady,
  Timeout,
  Stopped,
  Error,
};

struct DigitizerTelemetry {
  DeviceStatus device;
  std::uint64_t frames_received = 0;
  std::uint64_t dma_buffer_drops = 0;
  std::uint64_t trigger_misses = 0;
  double trigger_jitter_ns = 0.0;
};

class IDigitizer {
 public:
  virtual ~IDigitizer() = default;
  virtual std::string name() const = 0;
  virtual DigitizerTelemetry telemetry() const = 0;
  virtual bool connect(std::string& error) = 0;
  virtual void disconnect() = 0;
  virtual bool configure(const SystemConfig& config, std::string& error) = 0;
  virtual bool start(std::string& error) = 0;
  virtual FrameWaitResult waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout, std::string& error) = 0;
  virtual bool abort(std::string& error) = 0;
  virtual bool stop(std::string& error) = 0;
};

enum class EdfaMode {
  None,
  Manual,
  Controlled,
};

enum class EdfaControlMode {
  Acc,
  Apc,
  Agc,
};

enum class OpticalPowerUnit {
  Milliwatt,
  Dbm,
};

struct OpticalPowerSetpoint {
  double value = 0.0;
  OpticalPowerUnit unit = OpticalPowerUnit::Dbm;
};

struct EdfaStatus {
  DeviceStatus device;
  EdfaMode mode = EdfaMode::None;
  bool bypassed = true;
  bool required_before_start = false;
  EdfaControlMode control_mode = EdfaControlMode::Apc;
  OpticalPowerSetpoint setpoint;
  double measured_output_dbm = 0.0;
  bool output_enabled = false;
  bool interlock_closed = false;
  bool alarm_active = false;
  std::string alarm_code;
};

class IEdfaController {
 public:
  virtual ~IEdfaController() = default;
  virtual std::string name() const = 0;
  virtual EdfaStatus status() const = 0;
  virtual bool configure(const SystemConfig& config, std::string& error) = 0;
  virtual bool connect(std::string& error) = 0;
  virtual void disconnect() = 0;
  virtual bool setControlMode(EdfaControlMode mode, std::string& error) = 0;
  virtual bool setOutputSetpoint(const OpticalPowerSetpoint& setpoint, std::string& error) = 0;
  virtual bool setOutputEnabled(bool enabled, std::string& error) = 0;
  virtual bool resetAlarm(std::string& error) = 0;
  virtual bool emergencyOff(std::string& error) = 0;
};

class IMcuController {
 public:
  virtual ~IMcuController() = default;
  virtual std::string name() const = 0;
  virtual DeviceStatus status() const = 0;
  virtual bool connect(std::string& error) = 0;
  virtual void disconnect() = 0;
  virtual bool configure(const SystemConfig& config, std::string& error) = 0;
  virtual bool startScan(std::string& error) = 0;
  virtual bool stopScan(std::string& error) = 0;
  virtual bool emergencyStop(std::string& error) = 0;
};

class IUdpSender {
 public:
  virtual ~IUdpSender() = default;
  virtual DeviceStatus status() const = 0;
  virtual bool configure(const SystemConfig& config, std::string& error) = 0;
  virtual bool start(std::string& error) = 0;
  virtual bool send(const ProcessedFrame& frame, std::string& error) = 0;
  virtual void stop() = 0;
};

}  // namespace fmcw
