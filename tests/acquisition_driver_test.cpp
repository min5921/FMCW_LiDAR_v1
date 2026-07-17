#include "core/acquisition_session.h"
#include "core/continuous_acquisition_worker.h"
#include "core/raw_frame_batch_pool.h"
#include "drivers/alazar/alazar_digitizer.h"
#include "drivers/alazar/alazar_sample_conversion.h"
#include "drivers/edfa/edfa_protocol.h"
#include "drivers/edfa/edfa_serial_controller.h"
#include "drivers/mcu/mcu_protocol.h"
#include "drivers/mcu/mcu_serial_controller.h"
#include "drivers/runtime_adapter_factory.h"
#include "drivers/serial/serial_transport.h"
#include "drivers/simulator/fake_digitizer.h"
#include "drivers/simulator/fake_edfa.h"
#include "drivers/simulator/fake_mcu.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

class OrderedDigitizer final : public fmcw::IDigitizer {
 public:
  explicit OrderedDigitizer(std::vector<std::string>& events) : events_(events) {}

  std::string name() const override { return "ordered digitizer"; }
  fmcw::DigitizerTelemetry telemetry() const override { return telemetry_; }
  bool configure(const fmcw::SystemConfig&, std::string& error) override {
    error.clear();
    return true;
  }
  bool connect(std::string& error) override {
    telemetry_.device.connected = true;
    telemetry_.device.ready = true;
    error.clear();
    return true;
  }
  void disconnect() override { telemetry_.device = {}; }
  bool start(std::string& error) override {
    events_.push_back("digitizer.start");
    telemetry_.device.running = true;
    error.clear();
    return true;
  }
  fmcw::FrameWaitResult waitForBatch(fmcw::MutableRawFrameBatchPtr&,
                                     std::chrono::milliseconds,
                                     std::string& error) override {
    error.clear();
    return fmcw::FrameWaitResult::Timeout;
  }
  fmcw::FrameWaitResult waitForFrame(fmcw::RawFrame&, std::chrono::milliseconds,
                                     std::string& error) override {
    error.clear();
    return fmcw::FrameWaitResult::Timeout;
  }
  bool abort(std::string& error) override {
    events_.push_back("digitizer.abort");
    telemetry_.device.running = false;
    error.clear();
    return true;
  }
  bool stop(std::string& error) override {
    events_.push_back("digitizer.stop");
    telemetry_.device.running = false;
    error.clear();
    return true;
  }

 private:
  std::vector<std::string>& events_;
  fmcw::DigitizerTelemetry telemetry_;
};

class OrderedEdfa final : public fmcw::IEdfaController {
 public:
  explicit OrderedEdfa(std::vector<std::string>& events) : events_(events) {}

  std::string name() const override { return "ordered EDFA"; }
  fmcw::EdfaStatus status() const override { return status_; }
  bool configure(const fmcw::SystemConfig& config, std::string& error) override {
    status_.mode = config.edfa.mode;
    status_.bypassed = config.edfa.mode == fmcw::EdfaMode::None;
    error.clear();
    return true;
  }
  bool connect(std::string& error) override {
    status_.device.connected = true;
    status_.device.ready = true;
    error.clear();
    return true;
  }
  void disconnect() override { status_.device = {}; }
  bool setControlMode(fmcw::EdfaControlMode mode, std::string& error) override {
    status_.control_mode = mode;
    error.clear();
    return true;
  }
  bool setOutputSetpoint(const fmcw::OpticalPowerSetpoint& setpoint,
                         std::string& error) override {
    status_.setpoint = setpoint;
    error.clear();
    return true;
  }
  bool setOutputEnabled(bool enabled, std::string& error) override {
    events_.push_back(enabled ? "edfa.on" : "edfa.off");
    status_.output_enabled = enabled;
    error.clear();
    return true;
  }
  bool resetAlarm(std::string& error) override {
    error.clear();
    return true;
  }
  bool emergencyOff(std::string& error) override {
    events_.push_back("edfa.emergency_off");
    status_.output_enabled = false;
    error.clear();
    return true;
  }

 private:
  std::vector<std::string>& events_;
  fmcw::EdfaStatus status_;
};

class OrderedMcu final : public fmcw::IMcuController {
 public:
  explicit OrderedMcu(std::vector<std::string>& events) : events_(events) {}

  std::string name() const override { return "ordered MCU"; }
  fmcw::McuStatus status() const override { return status_; }
  bool configure(const fmcw::SystemConfig&, std::string& error) override {
    error.clear();
    return true;
  }
  bool connect(std::string& error) override {
    status_.device.connected = true;
    status_.device.ready = true;
    error.clear();
    return true;
  }
  void disconnect() override { status_.device = {}; }
  bool uploadWaveform(const std::vector<fmcw::McuWaveformFrame>& frames,
                      std::string& error) override {
    status_.waveform_points = static_cast<std::uint32_t>(frames.size());
    error.clear();
    return true;
  }
  bool startScan(std::string& error) override {
    events_.push_back("mcu.start");
    status_.scan_enabled = true;
    error.clear();
    return true;
  }
  bool stopScan(std::string& error) override {
    events_.push_back("mcu.stop");
    status_.scan_enabled = false;
    error.clear();
    return true;
  }
  bool emergencyStop(std::string& error) override {
    events_.push_back("mcu.emergency_stop");
    status_.scan_enabled = false;
    error.clear();
    return true;
  }

 private:
  std::vector<std::string>& events_;
  fmcw::McuStatus status_;
};

void testMcuProtocol() {
  const fmcw::McuWaveformFrame frame{1, 2, 3, 4, true};
  expect(fmcw::McuProtocol::clearCommand() == "CLR\n", "MCU clear command matches firmware");
  expect(fmcw::McuProtocol::dataCommand(frame) == "DATA,1,2,3,4,255\n",
         "MCU DATA command drives the firmware trigger output high");
  const auto loaded = fmcw::McuProtocol::parseResponse("ACK:LOAD_DONE,25");
  expect(loaded.acknowledged && loaded.code == "LOAD_DONE" && loaded.has_count && loaded.count == 25,
         "MCU LOAD_DONE count is parsed");
  expect(fmcw::McuProtocol::parseResponse("ERR:BUFFER_FULL").error, "MCU error response is recognized");

  fmcw::SystemConfig raster;
  raster.digitizer.records_per_buffer = 4;
  raster.digitizer.a_scan_count = 4;
  raster.scan.x_pixel_count = 4;
  raster.scan.y_line_count = 3;
  raster.digitizer.b_scan_count = 3;
  raster.scan.bidirectional = true;
  std::string waveform_error;
  const auto waveform = fmcw::McuProtocol::buildFullFrameWaveform(raster, waveform_error);
  expect(waveform.size() == 12, "MCU waveform contains one complete 4 by 3 raster frame");
  expect(waveform[0].trigger && waveform[4].trigger && waveform[8].trigger,
         "MCU waveform emits one marker at each B-scan boundary");
  expect(!waveform[1].trigger && !waveform[3].trigger && !waveform[5].trigger,
         "MCU waveform does not hold the marker across a B-scan");
  expect(waveform[0].a < waveform[3].a && waveform[4].a > waveform[7].a,
         "bidirectional raster reverses the X waveform on alternating B-scans");
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
  expect(frame.metadata.dma_buffer_sequence == 0U && frame.metadata.record_index_in_buffer == 0U &&
             frame.metadata.records_in_buffer == config.digitizer.records_per_buffer,
         "fake frame identifies its DMA buffer and record index");
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
  expect(telemetry.digitizer.frames_received == config.digitizer.records_per_buffer &&
             telemetry.digitizer.dma_buffers_received == 1,
         "legacy frame access still reports the full DMA batch received from the digitizer");
  expect(telemetry.edfa.bypassed && telemetry.edfa.device.ready, "core telemetry exposes EDFA bypass");
  expect(!telemetry.mcu.device.connected && telemetry.mcu.device.ready, "core telemetry exposes optional MCU bypass");
  expect(session.stop(error), "fake session stops cleanly");
}

void testFakeDmaBatchSession() {
  fmcw::FakeDigitizer digitizer;
  fmcw::FakeEdfaController edfa;
  fmcw::FakeMcuController mcu;
  fmcw::AcquisitionSession session(digitizer, edfa, mcu);
  fmcw::SystemConfig config;
  config.digitizer.records_per_buffer = 4;
  config.digitizer.a_scan_count = 4;
  config.scan.x_pixel_count = 4;
  std::string error;

  expect(session.configure(config, 18, error) && session.connect(error) && session.start(error),
         "batch session configures, connects, and starts");
  fmcw::RawFrameBatchPtr batch;
  expect(session.waitForBatch(batch, std::chrono::milliseconds(20), error) ==
             fmcw::FrameWaitResult::FrameReady,
         "fake digitizer returns one complete DMA batch");
  expect(batch && batch->records.size() == 4U && batch->metadata.record_count == 4U &&
             batch->metadata.record_length == config.digitizer.sample_point,
         "DMA batch owns all configured records and record-length metadata");
  expect(batch && batch->metadata.acquisition_wakeup_timestamp_ns >=
                         batch->metadata.completion_timestamp_ns &&
             batch->metadata.ownership_ready_timestamp_ns >=
                         batch->metadata.acquisition_wakeup_timestamp_ns &&
             batch->metadata.session_ready_timestamp_ns >=
                         batch->metadata.ownership_ready_timestamp_ns,
         "DMA batch timestamps completion, wakeup, ownership, and session readiness in order");
  bool metadata_consistent = batch != nullptr;
  for (std::size_t index = 0; batch && index < batch->records.size(); ++index) {
    const auto frame = fmcw::rawFrameAt(batch, index);
    metadata_consistent = metadata_consistent && frame &&
        frame->metadata.dma_buffer_sequence == batch->metadata.sequence &&
        frame->metadata.record_index_in_buffer == index &&
        frame->metadata.records_in_buffer == batch->records.size() &&
        frame->metadata.config_revision == 18;
  }
  expect(metadata_consistent, "all records retain immutable batch and session metadata");
  expect(session.stop(error), "batch session stops cleanly");
}

void testFakeDmaRingOverflow() {
  fmcw::FakeDigitizer digitizer;
  fmcw::SystemConfig config;
  config.digitizer.records_per_buffer = 2;
  config.digitizer.a_scan_count = 2;
  config.digitizer.dma_buffer_count = 2;
  config.scan.x_pixel_count = 2;
  config.laser.sweep_rate_hz = 1000.0;
  config.runtime.simulator_realtime_dma = true;
  std::string error;

  expect(digitizer.configure(config, error) && digitizer.connect(error) && digitizer.start(error),
         "bounded fake DMA ring starts");
  const auto overflow_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
  while (digitizer.telemetry().dma_buffer_drops == 0U &&
         std::chrono::steady_clock::now() < overflow_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  fmcw::MutableRawFrameBatchPtr batch;
  expect(digitizer.waitForBatch(batch, std::chrono::milliseconds(20), error) ==
             fmcw::FrameWaitResult::Error,
         "fake DMA ring reports overflow instead of slowing the producer");
  const auto telemetry = digitizer.telemetry();
  expect(telemetry.dma_buffer_drops == 1U &&
             telemetry.device.detail.find("overflow") != std::string::npos,
         "fake DMA overflow is latched in digitizer telemetry");
  expect(std::abs(telemetry.dma_buffer_period_ms - 2.0) < 0.01,
         "fake DMA cadence is derived from records per buffer and laser sweep rate");
  expect(digitizer.stop(error), "overflowed fake DMA ring stops cleanly");
}

void testAtsQualificationSimulatorLoad() {
  fmcw::FakeDigitizer digitizer;
  const auto config = fmcw::makeAts9371QualificationSimulatorConfig();
  std::string error;

  expect(config.runtime.simulator_realtime_dma &&
             config.digitizer.sample_point == 4992U &&
             config.digitizer.records_per_buffer == 998U &&
             config.chirp_segmentation.up_segment.length() == 2048U &&
             config.chirp_segmentation.down_segment.length() == 2048U,
         "ATS qualification simulator fixes the 4992 x 998 full-period workload");
  expect(digitizer.configure(config, error) && digitizer.connect(error) && digitizer.start(error),
         "ATS qualification simulator starts at strict DMA cadence");

  fmcw::MutableRawFrameBatchPtr batch;
  expect(digitizer.waitForBatch(batch, std::chrono::milliseconds(100), error) ==
             fmcw::FrameWaitResult::FrameReady,
         "ATS qualification simulator publishes one complete DMA batch");
  expect(batch && batch->records.size() == 998U &&
             batch->metadata.record_count == 998U &&
             batch->metadata.record_length == 4992U &&
             batch->records.front().samples.size() == 4992U &&
             batch->records.back().samples.size() == 4992U,
         "qualification DMA batch contains 998 complete 4992-sample records");
  const auto running_telemetry = digitizer.telemetry();
  expect(std::abs(running_telemetry.dma_buffer_period_ms - 4.99) < 0.01 &&
             std::abs(running_telemetry.dma_buffer_rate_hz - (1000.0 / 4.99)) < 0.5,
         "qualification DMA batch arrives at the 998 / 200 kHz interval");

  std::size_t positive_noise_samples = 0U;
  std::size_t negative_noise_samples = 0U;
  std::uint64_t noise_sum_squares = 0U;
  if (batch && !batch->records.empty()) {
    const auto& samples = batch->records.front().samples;
    for (std::uint32_t index = config.chirp_segmentation.up_segment.end_sample_exclusive;
         index < config.chirp_segmentation.down_segment.start_sample; ++index) {
      positive_noise_samples += samples[index] > 0 ? 1U : 0U;
      negative_noise_samples += samples[index] < 0 ? 1U : 0U;
      const auto sample = static_cast<std::int64_t>(samples[index]);
      noise_sum_squares += static_cast<std::uint64_t>(sample * sample);
    }
  }
  const auto quiet_sample_count =
      config.chirp_segmentation.down_segment.start_sample -
      config.chirp_segmentation.up_segment.end_sample_exclusive;
  expect(positive_noise_samples > quiet_sample_count / 3U &&
             negative_noise_samples > quiet_sample_count / 3U,
         "qualification simulator adds bipolar ADC noise outside the chirp segments");
  const double quiet_noise_rms = quiet_sample_count == 0U
      ? 0.0
      : std::sqrt(static_cast<double>(noise_sum_squares) /
                  static_cast<double>(quiet_sample_count));
  expect(quiet_noise_rms >= 80.0 && quiet_noise_rms <= 112.0,
         "qualification simulator keeps quiet-region noise near 96 ADC counts RMS");

  std::vector<std::int16_t> first_waveform;
  if (batch && !batch->records.empty()) {
    first_waveform.assign(batch->records.front().samples.begin(),
                          batch->records.front().samples.end());
  }
  batch.reset();
  expect(digitizer.waitForBatch(batch, std::chrono::milliseconds(20), error) ==
             fmcw::FrameWaitResult::FrameReady && batch && !batch->records.empty() &&
             !(batch->records.front().samples == first_waveform),
         "qualification simulator varies the displayed waveform between DMA slots");
  batch.reset();
  std::this_thread::sleep_for(std::chrono::milliseconds(55));
  expect(digitizer.waitForBatch(batch, std::chrono::milliseconds(20), error) ==
             fmcw::FrameWaitResult::Error &&
             digitizer.telemetry().dma_buffer_drops == 1U,
         "unserviced qualification workload overflows the eight-buffer DMA ring");
  expect(digitizer.stop(error), "qualification simulator stops after the overflow test");
}

void testGlobalStopDeviceOrder() {
  std::vector<std::string> events;
  OrderedDigitizer digitizer(events);
  OrderedEdfa edfa(events);
  OrderedMcu mcu(events);
  fmcw::AcquisitionSession session(digitizer, edfa, mcu);
  fmcw::SystemConfig config;
  config.edfa.mode = fmcw::EdfaMode::Controlled;
  config.edfa.port = "ORDERED";
  config.edfa.warmup_delay_ms = 0;
  config.mcu.enabled = true;
  config.mcu.port = "ORDERED";
  std::string error;
  expect(session.configure(config, 20, error) && session.connect(error) && session.start(error),
         "ordered hardware session configures, connects, and starts");
  events.clear();
  expect(session.stop(error), "ordered hardware session stops cleanly");
  const std::vector<std::string> expected{
      "mcu.stop", "digitizer.abort", "digitizer.stop", "edfa.off"};
  expect(events == expected,
         "global Stop removes the MCU trigger, aborts and stops DMA, then disables EDFA output");
}

void testRawFrameBatchPoolLifetime() {
  fmcw::RawFrameBatchPool pool(1);
  auto mutable_batch = pool.acquire();
  auto* original_address = mutable_batch.get();
  mutable_batch->records.resize(1);
  mutable_batch->records.front().samples.resize(4096);
  const auto original_capacity = mutable_batch->records.front().samples.capacity();
  fmcw::RawFrameBatchPtr batch = mutable_batch;
  auto frame = fmcw::rawFrameAt(batch, 0);
  mutable_batch.reset();
  batch.reset();
  expect(pool.cachedBatchCount() == 0U,
         "an aliased record keeps its pooled DMA batch alive");
  frame.reset();
  expect(pool.cachedBatchCount() == 1U,
         "DMA batch returns to the pool after the final record reference is released");
  auto reused = pool.acquire();
  expect(reused.get() == original_address && reused->records.front().samples.capacity() == original_capacity,
         "batch pool reuses the batch object and retained sample allocation");
  reused.reset();

  std::vector<std::int16_t> external_samples(16U, 7);
  auto lease_token = std::make_shared<int>(42);
  std::weak_ptr<int> lease_lifetime = lease_token;
  auto external_batch = pool.acquire();
  external_batch->metadata.record_count = 1U;
  external_batch->metadata.record_length = static_cast<std::uint32_t>(external_samples.size());
  external_batch->contiguous_samples.setView(external_samples.data(), external_samples.size());
  external_batch->records.resize(1U);
  external_batch->records.front().samples.setView(external_samples.data(), external_samples.size());
  external_batch->sample_owner = lease_token;
  lease_token.reset();
  expect(external_batch->hasExternalSampleStorage() && !lease_lifetime.expired(),
         "external DMA sample storage remains leased while the batch is alive");
  external_batch.reset();
  expect(lease_lifetime.expired(),
         "returning a batch to the pool releases its external DMA lease immediately");
  auto after_external = pool.acquire();
  expect(!after_external->hasExternalSampleStorage() &&
             after_external->records.front().samples.empty(),
         "pooled DMA batches never expose a stale external sample view");
}

void testContinuousAcquisitionWorker() {
  fmcw::FakeDigitizer digitizer;
  fmcw::FakeEdfaController edfa;
  fmcw::FakeMcuController mcu;
  fmcw::AcquisitionSession session(digitizer, edfa, mcu);
  fmcw::SystemConfig config;
  config.digitizer.acquisition_mode = fmcw::AcquisitionMode::Finite;
  config.digitizer.finite_frame_count = 5;
  config.digitizer.records_per_buffer = 4;
  config.digitizer.a_scan_count = 4;
  config.scan.x_pixel_count = 4;
  std::string error;
  expect(session.configure(config, 19, error) && session.connect(error) && session.start(error),
         "finite session starts for continuous worker test");

  fmcw::ContinuousAcquisitionWorker worker(session);
  std::mutex mutex;
  std::condition_variable condition;
  bool exited = false;
  bool failed = true;
  std::uint64_t records = 0;
  expect(worker.start(
      [&](fmcw::RawFrameBatchPtr batch, std::string&) {
        std::lock_guard<std::mutex> lock(mutex);
        records += batch->records.size();
        return true;
      },
      [&](bool worker_failed, std::string) {
        std::lock_guard<std::mutex> lock(mutex);
        failed = worker_failed;
        exited = true;
        condition.notify_all();
      },
      error), "continuous acquisition worker starts");
  {
    std::unique_lock<std::mutex> lock(mutex);
    expect(condition.wait_for(lock, std::chrono::seconds(2), [&] { return exited; }),
           "continuous worker reports finite source completion");
  }
  expect(worker.waitUntilStopped(error), "continuous worker joins without an acquisition failure");
  const auto status = worker.status();
  expect(!failed && records == 5U && status.batches_delivered == 2U &&
             status.records_delivered == 5U,
         "continuous worker delivers a full and partial final batch without polling");
  expect(session.stop(error), "finite worker session finalizes cleanly");
}

void testRuntimeAdapterFactory() {
  const auto simulator = fmcw::createRuntimeAdapters(fmcw::AcquisitionSource::Simulator);
  const auto replay = fmcw::createRuntimeAdapters(fmcw::AcquisitionSource::Replay);
  const auto hardware = fmcw::createRuntimeAdapters(fmcw::AcquisitionSource::Alazar);
  expect(simulator && simulator.digitizer->name().find("Fake") != std::string::npos,
         "simulator source creates fake device adapters");
  expect(replay && replay.digitizer->name().find("replay") != std::string::npos,
         "replay source creates the raw replay adapter");
  expect(hardware && hardware.digitizer->name().find("AlazarTech") != std::string::npos,
         "hardware source creates the real Alazar adapter");
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
  config.edfa.warmup_delay_ms = 0;
  config.mcu.enabled = true;
  config.mcu.port = "SIM";
  std::string error;

  expect(session.configure(config, 22, error), "controlled fake session configures");
  expect(session.connect(error), "controlled fake session connects");
  expect(mcu.uploadWaveform({{1, 2, 3, 4, true}}, error), "fake MCU waveform loads before Start");
  expect(session.start(error), "controlled EDFA starts, digitizer arms, and MCU scan starts last");
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

void testScannerStartFailureRollsBackArmedDevices() {
  fmcw::FakeDigitizer digitizer;
  fmcw::FakeEdfaController edfa;
  fmcw::FakeMcuController mcu;
  fmcw::AcquisitionSession session(digitizer, edfa, mcu);
  fmcw::SystemConfig config;
  config.edfa.mode = fmcw::EdfaMode::Controlled;
  config.edfa.port = "SIM";
  config.edfa.warmup_delay_ms = 0;
  config.mcu.enabled = true;
  config.mcu.port = "SIM";
  std::string error;

  expect(session.configure(config, 23, error) && session.connect(error),
         "rollback test session configures and connects");
  expect(!session.start(error), "Start fails when the MCU waveform is not loaded");
  expect(!digitizer.telemetry().device.running && !edfa.status().output_enabled && !mcu.status().scan_enabled,
         "failed scanner Start rolls back the armed digitizer and EDFA output");
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

void testAlazarLeftAlignedSampleConversion() {
  expect(fmcw::alazarLeftAlignedSampleToSignedInt16(0x0000U, 12U) == -32768,
         "12-bit Alazar minimum code maps to signed full-scale minimum");
  expect(fmcw::alazarLeftAlignedSampleToSignedInt16(0x8000U, 12U) == 0,
         "12-bit Alazar midpoint maps to signed zero");
  expect(fmcw::alazarLeftAlignedSampleToSignedInt16(0xFFF0U, 12U) == 32752,
         "12-bit Alazar maximum code preserves its signed full-scale step");
  expect(fmcw::alazarLeftAlignedSampleToSignedInt16(0xFFFFU, 12U) == 32752 &&
             fmcw::alazarLeftAlignedSampleToSignedInt16(0x800FU, 12U) == 0,
         "12-bit conversion ignores the four DMA padding bits");
  expect(fmcw::alazarLeftAlignedSampleToSignedInt16(0x0000U, 16U) == -32768 &&
             fmcw::alazarLeftAlignedSampleToSignedInt16(0x8000U, 16U) == 0 &&
             fmcw::alazarLeftAlignedSampleToSignedInt16(0xFFFFU, 16U) == 32767,
         "16-bit conversion reaches both signed endpoints without overflow");
  expect(fmcw::alazarLeftAlignedSampleToSignedInt16(0x8000U, 0U) == 0 &&
             fmcw::alazarLeftAlignedSampleToSignedInt16(0x8000U, 17U) == 0,
         "unsupported sample widths fail closed");

  bool matches_legacy_full_scale = true;
  for (std::uint32_t code = 0; code < 4096U; ++code) {
    const auto raw = static_cast<std::uint16_t>(code << 4U);
    const auto legacy_value = static_cast<std::int32_t>(raw) - 32768;
    matches_legacy_full_scale = matches_legacy_full_scale &&
        fmcw::alazarLeftAlignedSampleToSignedInt16(raw, 12U) == legacy_value;
  }
  expect(matches_legacy_full_scale,
         "SDK 12-bit code conversion matches legacy raw-minus-32768 full-scale samples");

  bool native_format_matches_sdk = true;
  for (const auto raw : {std::uint16_t{0x0000U}, std::uint16_t{0x8000U},
                         std::uint16_t{0x800FU}, std::uint16_t{0xFFF0U},
                         std::uint16_t{0xFFFFU}}) {
    native_format_matches_sdk = native_format_matches_sdk &&
        fmcw::sampleAsSignedInt16(static_cast<std::int16_t>(raw),
                                 fmcw::SampleFormat::UnsignedOffsetBinary12LeftAligned) ==
            fmcw::alazarLeftAlignedSampleToSignedInt16(raw, 12U);
  }
  expect(native_format_matches_sdk,
         "native ATS DMA sample format decodes exactly like the SDK 12-bit conversion");
}

}  // namespace

int main() {
  testMcuProtocol();
  testEdfaProtocol();
  testSerialControllers();
  testFakeFullPeriodSession();
  testFakeDmaBatchSession();
  testFakeDmaRingOverflow();
  testAtsQualificationSimulatorLoad();
  testGlobalStopDeviceOrder();
  testRawFrameBatchPoolLifetime();
  testContinuousAcquisitionWorker();
  testRuntimeAdapterFactory();
  testOptionalControlledDevicesAndChannelB();
  testScannerStartFailureRollsBackArmedDevices();
  testFiniteFakeAcquisition();
  testAlazarBuildGate();
  testAlazarLeftAlignedSampleConversion();

  if (failures == 0) {
    std::cout << "All acquisition and device driver tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
