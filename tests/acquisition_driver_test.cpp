#include "core/acquisition_session.h"
#include "drivers/alazar/alazar_digitizer.h"
#include "drivers/edfa/edfa_protocol.h"
#include "drivers/edfa/edfa_serial_controller.h"
#include "drivers/mcu/mcu_protocol.h"
#include "drivers/mcu/mcu_serial_controller.h"
#include "drivers/serial/serial_transport.h"
#include "drivers/simulator/fake_digitizer.h"
#include "drivers/simulator/fake_edfa.h"
#include "drivers/simulator/fake_mcu.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::uint8_t sum(const std::vector<std::uint8_t>& bytes) {
  std::uint32_t value = 0;
  for (const auto byte : bytes) {
    value += byte;
  }
  return static_cast<std::uint8_t>(value & 0xFFU);
}

std::vector<std::uint8_t> edfaResponse(std::uint8_t address, const std::vector<std::uint8_t>& data) {
  std::vector<std::uint8_t> bytes{0xED, 0xFA, static_cast<std::uint8_t>(data.size() + 2U), address};
  bytes.insert(bytes.end(), data.begin(), data.end());
  bytes.push_back(sum(bytes));
  return bytes;
}

class ScriptedSerialTransport final : public fmcw::ISerialTransport {
 public:
  bool open(const fmcw::SerialSettings& settings, std::string& error) override {
    open_ = !settings.port.empty();
    error = open_ ? std::string{} : "missing scripted port";
    return open_;
  }

  void close() override { open_ = false; }

  bool isOpen() const override { return open_; }

  bool purge(std::string& error) override {
    bytes_.clear();
    lines_.clear();
    error.clear();
    return open_;
  }

  bool write(const std::vector<std::uint8_t>& data, std::string& error) override {
    if (!open_ || data.empty()) {
      error = "scripted transport is closed or received empty write";
      return false;
    }
    if (data[0] == 0xEF) {
      scriptEdfa(data);
    } else {
      scriptMcu(std::string(data.begin(), data.end()));
    }
    error.clear();
    return true;
  }

  bool readExact(std::size_t byte_count, std::vector<std::uint8_t>& data,
                 std::chrono::milliseconds, std::string& error) override {
    if (bytes_.size() < byte_count) {
      error = "scripted binary response exhausted";
      return false;
    }
    data.clear();
    for (std::size_t index = 0; index < byte_count; ++index) {
      data.push_back(bytes_.front());
      bytes_.pop_front();
    }
    error.clear();
    return true;
  }

  bool readLine(std::string& line, std::chrono::milliseconds, std::string& error) override {
    if (lines_.empty()) {
      error = "scripted line response exhausted";
      return false;
    }
    line = lines_.front();
    lines_.pop_front();
    error.clear();
    return true;
  }

 private:
  void queueBytes(const std::vector<std::uint8_t>& bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  void scriptEdfa(const std::vector<std::uint8_t>& command) {
    const auto address = command.at(3);
    if (address == 0x00) {
      queueBytes(edfaResponse(0x00, {0x03, 0xE8, 0x00, 0x00, 0x17, 0x70,
                                     0x23, 0x28, 0x00, 0x00, 0x00, 0x00}));
    } else if (address == 0x06) {
      queueBytes(edfaResponse(0x05, {command.at(4)}));
    } else if (address == 0x04) {
      queueBytes(edfaResponse(0x03, {command.at(4), command.at(5)}));
    } else if (address == 0x26) {
      queueBytes(edfaResponse(0x25, {command.at(4)}));
    }
  }

  void scriptMcu(const std::string& command) {
    if (command == "CLR\n") {
      data_count_ = 0;
      lines_.push_back("ACK:CLR");
    } else if (command.rfind("DATA,", 0) == 0) {
      ++data_count_;
    } else if (command == "LOAD_DONE\n") {
      lines_.push_back("ACK:LOAD_DONE," + std::to_string(data_count_));
    } else if (command == "START\n") {
      lines_.push_back("ACK:START");
    } else if (command == "STOP\n") {
      lines_.push_back("ACK:STOP");
    }
  }

  bool open_ = false;
  std::uint32_t data_count_ = 0;
  std::deque<std::uint8_t> bytes_;
  std::deque<std::string> lines_;
};

void testMcuProtocol() {
  const fmcw::McuWaveformFrame frame{1, 2, 3, 4, true};
  expect(fmcw::McuProtocol::clearCommand() == "CLR\n", "MCU clear command matches firmware");
  expect(fmcw::McuProtocol::dataCommand(frame) == "DATA,1,2,3,4,1\n", "MCU DATA command matches firmware");
  const auto loaded = fmcw::McuProtocol::parseResponse("ACK:LOAD_DONE,25");
  expect(loaded.acknowledged && loaded.code == "LOAD_DONE" && loaded.has_count && loaded.count == 25,
         "MCU LOAD_DONE count is parsed");
  expect(fmcw::McuProtocol::parseResponse("ERR:BUFFER_FULL").error, "MCU error response is recognized");
}

void testEdfaProtocol() {
  expect(fmcw::EdfaProtocol::queryStatus() == std::vector<std::uint8_t>({0xEF, 0xEF, 0x02, 0x00, 0xE0}),
         "EDFA status query matches vendor example");
  expect(fmcw::EdfaProtocol::setTargetPowerDbm(19.99) ==
             std::vector<std::uint8_t>({0xEF, 0xEF, 0x04, 0x04, 0x23, 0x27, 0x30}),
         "EDFA target power command matches vendor example");
  expect(fmcw::EdfaProtocol::setActivation(true) ==
             std::vector<std::uint8_t>({0xEF, 0xEF, 0x03, 0x26, 0x01, 0x08}),
         "EDFA activation command matches vendor example");

  const std::vector<std::uint8_t> response{0xED, 0xFA, 0x0E, 0x00, 0x03, 0xE8, 0x00, 0x00,
                                            0x17, 0x70, 0x23, 0x28, 0x00, 0x00, 0x00, 0x00, 0xB2};
  fmcw::EdfaPacket packet;
  std::string error;
  expect(fmcw::EdfaProtocol::parseResponse(response, packet, error), "EDFA vendor status response parses");
  fmcw::EdfaDeviceReading reading;
  expect(fmcw::EdfaProtocol::decodeStatus(packet, reading, error), "EDFA status data decodes");
  expect(reading.current_ma == 1000.0 && reading.input_power_dbm == -10.0 && reading.output_power_dbm == 20.0,
         "EDFA current and optical power match vendor example");

  auto corrupted = response;
  corrupted.back() ^= 0x01;
  expect(!fmcw::EdfaProtocol::parseResponse(corrupted, packet, error), "EDFA checksum corruption is rejected");
}

void testSerialControllers() {
  std::string error;
  auto mcu_transport = std::make_shared<ScriptedSerialTransport>();
  fmcw::McuSerialController mcu(mcu_transport);
  fmcw::SystemConfig mcu_config;
  mcu_config.mcu.enabled = true;
  mcu_config.mcu.port = "SCRIPT";
  expect(mcu.configure(mcu_config, error), "MCU serial controller configures");
  expect(mcu.connect(error), "MCU serial controller connects");
  const std::vector<fmcw::McuWaveformFrame> frames{{1, 2, 3, 4, true}, {5, 6, 7, 8, false}};
  expect(mcu.uploadWaveform(frames, error), "MCU waveform upload completes with count acknowledgement");
  expect(mcu.status().waveform_points == 2 && mcu.status().device.ready, "MCU telemetry exposes loaded count");
  expect(mcu.startScan(error) && mcu.status().scan_enabled, "MCU START acknowledgement enables scan state");
  expect(mcu.stopScan(error) && !mcu.status().scan_enabled, "MCU STOP acknowledgement clears scan state");

  auto edfa_transport = std::make_shared<ScriptedSerialTransport>();
  fmcw::EdfaSerialController edfa(edfa_transport);
  fmcw::SystemConfig edfa_config;
  edfa_config.edfa.mode = fmcw::EdfaMode::Controlled;
  edfa_config.edfa.port = "SCRIPT";
  edfa_config.edfa.output_setpoint = {19.99, fmcw::OpticalPowerUnit::Dbm};
  expect(edfa.configure(edfa_config, error), "EDFA serial controller configures");
  expect(edfa.connect(error), "EDFA status query confirms connection");
  expect(edfa.status().measured_output_dbm == 20.0, "EDFA telemetry contains measured output");
  expect(!edfa.status().interlock_closed, "undefined vendor interlock state is not inferred from connectivity");
  expect(edfa.setControlMode(fmcw::EdfaControlMode::Apc, error), "EDFA APC mode is confirmed");
  expect(edfa.setOutputSetpoint(edfa_config.edfa.output_setpoint, error), "EDFA output setpoint is confirmed");
  expect(edfa.setOutputEnabled(true, error) && edfa.status().output_enabled,
         "EDFA activation acknowledgement enables output state");
  expect(edfa.emergencyOff(error) && !edfa.status().output_enabled, "EDFA emergency off confirms shutdown");
}

void testFakeFullPeriodSession() {
  fmcw::FakeDigitizer digitizer;
  fmcw::FakeEdfaController edfa;
  fmcw::FakeMcuController mcu;
  fmcw::AcquisitionSession session(digitizer, edfa, mcu);
  fmcw::SystemConfig config;
  std::string error;

  expect(session.configure(config, 17, error), "fake session configures with EDFA and MCU disabled");
  expect(session.connect(error), "fake session connects with optional devices bypassed");
  expect(session.start(error), "fake session starts without EDFA or MCU hardware");

  fmcw::RawFrame frame;
  expect(session.waitForFrame(frame, std::chrono::milliseconds(10), error) == fmcw::FrameWaitResult::FrameReady,
         "fake digitizer returns a frame");
  expect(frame.samples.size() == config.digitizer.sample_point, "fake frame contains one full record");
  expect(frame.metadata.frame_kind == fmcw::FrameKind::FullChirpPeriod, "fake frame is full-period");
  expect(frame.metadata.channel == fmcw::DigitizerChannel::A, "fake session acquires channel A only");
  expect(frame.metadata.up_segment.start_sample == config.chirp_segmentation.up_segment.start_sample &&
             frame.metadata.down_segment.end_sample_exclusive ==
                 config.chirp_segmentation.down_segment.end_sample_exclusive,
         "fake frame carries up/down segment boundaries");
  expect(frame.metadata.config_revision == 17 && !frame.metadata.optical_state.edfa_used,
         "session stamps config revision and EDFA bypass state");
  expect(std::any_of(frame.samples.begin() + frame.metadata.up_segment.start_sample,
                     frame.samples.begin() + frame.metadata.up_segment.end_sample_exclusive,
                     [](std::int16_t value) { return value != 0; }),
         "fake up-chirp segment contains deterministic signal data");

  const auto telemetry = session.telemetry();
  expect(telemetry.digitizer.frames_received == 1, "core telemetry exposes digitizer frame count");
  expect(telemetry.edfa.bypassed && telemetry.edfa.device.ready, "core telemetry exposes EDFA bypass");
  expect(!telemetry.mcu.device.connected && telemetry.mcu.device.ready, "core telemetry exposes optional MCU bypass");
  expect(session.stop(error), "fake session stops cleanly");
}

void testOptionalControlledDevicesAndChannelB() {
  fmcw::FakeDigitizer digitizer;
  fmcw::FakeEdfaController edfa;
  fmcw::FakeMcuController mcu;
  fmcw::AcquisitionSession session(digitizer, edfa, mcu);
  fmcw::SystemConfig config;
  config.digitizer.channel = fmcw::DigitizerChannel::B;
  config.edfa.mode = fmcw::EdfaMode::Controlled;
  config.edfa.port = "SIM";
  config.mcu.enabled = true;
  config.mcu.port = "SIM";
  std::string error;

  expect(session.configure(config, 22, error), "controlled fake session configures");
  expect(session.connect(error), "controlled fake session connects");
  expect(mcu.uploadWaveform({{1, 2, 3, 4, true}}, error), "fake MCU waveform loads before Start");
  expect(session.start(error), "controlled EDFA and MCU start before acquisition");
  fmcw::RawFrame frame;
  expect(session.waitForFrame(frame, std::chrono::milliseconds(10), error) == fmcw::FrameWaitResult::FrameReady,
         "controlled fake session returns a frame");
  expect(frame.metadata.channel == fmcw::DigitizerChannel::B, "single-channel B acquisition is supported");
  expect(frame.metadata.optical_state.edfa_used && frame.metadata.optical_state.edfa_output_enabled,
         "frame records controlled EDFA output state");
  expect(session.telemetry().mcu.scan_enabled, "core telemetry exposes active MCU scan state");
  expect(session.stop(error), "controlled session stops devices");
  expect(!edfa.status().output_enabled && !mcu.status().scan_enabled, "Stop disables EDFA output and MCU scan");
}

void testFiniteFakeAcquisition() {
  fmcw::FakeDigitizer digitizer;
  fmcw::SystemConfig config;
  config.digitizer.acquisition_mode = fmcw::AcquisitionMode::Finite;
  config.digitizer.finite_frame_count = 2;
  std::string error;
  expect(digitizer.configure(config, error) && digitizer.connect(error) && digitizer.start(error),
         "finite fake digitizer starts");
  fmcw::RawFrame frame;
  expect(digitizer.waitForFrame(frame, std::chrono::milliseconds(10), error) == fmcw::FrameWaitResult::FrameReady,
         "finite fake digitizer returns first frame");
  expect(digitizer.waitForFrame(frame, std::chrono::milliseconds(10), error) == fmcw::FrameWaitResult::FrameReady,
         "finite fake digitizer returns final frame");
  expect(digitizer.waitForFrame(frame, std::chrono::milliseconds(10), error) == fmcw::FrameWaitResult::Stopped,
         "finite fake digitizer stops at configured frame count");
}

void testAlazarBuildGate() {
  if (fmcw::AlazarDigitizer::sdkAvailable()) {
    return;
  }
  fmcw::AlazarDigitizer digitizer;
  fmcw::SystemConfig config;
  std::string error;
  expect(digitizer.configure(config, error), "no-SDK Alazar adapter still validates and caches configuration");
  expect(!digitizer.connect(error) && error.find("ALAZAR_SDK_ROOT") != std::string::npos,
         "no-SDK Alazar adapter reports an actionable connection error");
}

}  // namespace

int main() {
  testMcuProtocol();
  testEdfaProtocol();
  testSerialControllers();
  testFakeFullPeriodSession();
  testOptionalControlledDevicesAndChannelB();
  testFiniteFakeAcquisition();
  testAlazarBuildGate();

  if (failures == 0) {
    std::cout << "All acquisition and device driver tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
