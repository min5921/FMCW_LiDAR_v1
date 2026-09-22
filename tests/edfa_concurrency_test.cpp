#include "core/acquisition/acquisition_session.h"
#include "drivers/edfa/edfa_serial_controller.h"
#include "drivers/simulator/fake_digitizer.h"
#include "drivers/simulator/fake_mcu.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

class GatedEdfaTransport final : public fmcw::ISerialTransport {
 public:
  bool open(const fmcw::SerialSettings&, std::string& error) override {
    open_ = true;
    error.clear();
    return true;
  }
  void close() override { open_ = false; }
  bool isOpen() const override { return open_; }
  bool purge(std::string& error) override {
    response_.clear();
    error.clear();
    return true;
  }
  bool write(const std::vector<std::uint8_t>& data, std::string& error) override {
    if (!open_ || data.size() < 5U) {
      error = "Mock EDFA is closed or command is invalid";
      return false;
    }
    ++writes;
    address_ = data[3];
    switch (address_) {
      case 0x00: {
        const auto current = current_ma.load();
        queue(0x00, {static_cast<std::uint8_t>(current >> 8U),
                     static_cast<std::uint8_t>(current & 0xFFU),
                     0, 0, 0x17, 0x70, 0x23, 0x28, 0, 0, 0, 0});
        break;
      }
      case 0x05: queue(0x05, {mode_}); break;
      case 0x03: queue(0x03, {power_high_, power_low_}); break;
      case 0x25: queue(0x25, {enabled_}); break;
      case 0x06:
        mode_ = data.at(4);
        queue(0x05, {mode_});
        break;
      case 0x04:
        power_high_ = data.at(4);
        power_low_ = data.at(5);
        queue(0x03, {power_high_, power_low_});
        break;
      case 0x26:
        enabled_ = data.at(4);
        queue(0x25, {enabled_});
        break;
      default:
        error = "Unexpected EDFA mock address";
        return false;
    }
    error.clear();
    return true;
  }
  bool readExact(std::size_t count, std::vector<std::uint8_t>& data,
                 std::chrono::milliseconds, std::string& error) override {
    {
      std::unique_lock<std::mutex> lock(gate_mutex_);
      if (gate_armed_ && address_ == gate_address_) {
        gate_armed_ = false;
        blocked_ = true;
        gate_cv_.notify_all();
        if (!gate_cv_.wait_for(lock, 5s, [this] { return released_; }) || fail_read_) {
          response_.clear();
          error = "Injected EDFA serial timeout";
          return false;
        }
      }
    }
    if (response_.size() < count) {
      error = "Mock EDFA response exhausted";
      return false;
    }
    data.clear();
    while (data.size() < count) {
      data.push_back(response_.front());
      response_.pop_front();
    }
    error.clear();
    return true;
  }
  bool readLine(std::string&, std::chrono::milliseconds, std::string& error) override {
    error = "EDFA uses binary responses";
    return false;
  }
  void gateNextRead(std::uint8_t address, bool fail_read = false) {
    std::lock_guard<std::mutex> lock(gate_mutex_);
    gate_address_ = address;
    fail_read_ = fail_read;
    blocked_ = false;
    released_ = false;
    gate_armed_ = true;
  }
  bool waitForBlockedRead() {
    std::unique_lock<std::mutex> lock(gate_mutex_);
    return gate_cv_.wait_for(lock, 2s, [this] { return blocked_; });
  }
  void releaseRead() {
    std::lock_guard<std::mutex> lock(gate_mutex_);
    released_ = true;
    gate_cv_.notify_all();
  }

  std::atomic<unsigned> writes{0};
  std::atomic<std::uint16_t> current_ma{1000};

 private:
  void queue(std::uint8_t address, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> packet{0xED, 0xFA,
        static_cast<std::uint8_t>(payload.size() + 2U), address};
    packet.insert(packet.end(), payload.begin(), payload.end());
    std::uint8_t checksum = 0;
    for (const auto value : packet) {
      checksum = static_cast<std::uint8_t>(checksum + value);
    }
    packet.push_back(checksum);
    response_.insert(response_.end(), packet.begin(), packet.end());
  }

  bool open_ = false;
  std::uint8_t address_ = 0;
  std::uint8_t mode_ = 0;
  std::uint8_t enabled_ = 0;
  std::uint8_t power_high_ = 0x23;
  std::uint8_t power_low_ = 0x27;
  std::deque<std::uint8_t> response_;
  std::mutex gate_mutex_;
  std::condition_variable gate_cv_;
  std::uint8_t gate_address_ = 0;
  bool gate_armed_ = false;
  bool fail_read_ = false;
  bool blocked_ = false;
  bool released_ = false;
};

fmcw::SystemConfig testConfig() {
  fmcw::SystemConfig config;
  config.edfa.mode = fmcw::EdfaMode::Controlled;
  config.edfa.port = "MOCK_EDFA";
  config.edfa.warmup_delay_ms = 0;
  config.edfa.output_setpoint = {19.99, fmcw::OpticalPowerUnit::Dbm};
  config.digitizer.records_per_buffer = 4;
  config.digitizer.a_scan_count = 4;
  config.scan.x_pixel_count = 4;
  return config;
}

struct CommandObservation {
  fmcw::EdfaStatus pending;
  bool succeeded;
  std::string error;
};

template <typename Command>
CommandObservation observePendingCommand(fmcw::EdfaSerialController& edfa,
                                         GatedEdfaTransport& transport,
                                         const std::string& label, Command command) {
  auto operation = std::async(std::launch::async, [command] {
    std::string error;
    const bool succeeded = command(error);
    return std::make_pair(succeeded, error);
  });
  expect(transport.waitForBlockedRead(), label + " reaches gated serial read");
  auto reader = std::async(std::launch::async, [&edfa] { return edfa.status(); });
  // Completion while the gate is still closed proves independence from I/O,
  // without asserting a sub-millisecond scheduler deadline on the test host.
  expect(reader.wait_for(500ms) == std::future_status::ready,
         label + " permits status reads before UART completes");
  transport.releaseRead();
  auto pending = reader.get();
  auto result = operation.get();
  return {std::move(pending), result.first, std::move(result.second)};
}

void testStatusDuringCommands() {
  auto transport = std::make_shared<GatedEdfaTransport>();
  fmcw::EdfaSerialController edfa(transport);
  std::string error;
  expect(edfa.configure(testConfig(), error), "EDFA configures");

  transport->gateNextRead(0x00);
  const auto connection = observePendingCommand(edfa, *transport, "Connect",
      [&edfa](std::string& e) { return edfa.connect(e); });
  expect(connection.succeeded && !connection.pending.device.connected &&
             edfa.status().device.connected, "Connection publishes only after confirmation");
  const auto initial_timestamp = edfa.status().telemetry_timestamp_ns;
  expect(connection.pending.telemetry_timestamp_ns == 0U && initial_timestamp > 0U,
         "Confirmed telemetry carries a host freshness timestamp");

  transport->current_ma = 1100;
  transport->gateNextRead(0x25);
  const auto poll = observePendingCommand(edfa, *transport, "Poll",
      [&edfa](std::string& e) { return edfa.pollStatus(e); });
  expect(poll.succeeded && poll.pending.measured_current_ma == 1000.0 &&
             edfa.status().measured_current_ma == 1100.0,
         "A full poll publishes one coherent status, not intermediate query results");
  expect(poll.pending.telemetry_timestamp_ns == initial_timestamp &&
             edfa.status().telemetry_timestamp_ns >= initial_timestamp,
         "Pending polls retain the previous telemetry timestamp");

  transport->gateNextRead(0x06);
  const auto mode = observePendingCommand(edfa, *transport, "Mode",
      [&edfa](std::string& e) { return edfa.setControlMode(fmcw::EdfaControlMode::Apc, e); });
  expect(mode.succeeded, "Mode command is still confirmed");

  transport->gateNextRead(0x04);
  const auto power = observePendingCommand(edfa, *transport, "Power",
      [&edfa](std::string& e) {
        return edfa.setOutputSetpoint({20.0, fmcw::OpticalPowerUnit::Dbm}, e);
      });
  expect(power.succeeded && std::abs(power.pending.setpoint.value - 19.99) < 0.001 &&
             std::abs(edfa.status().setpoint.value - 20.0) < 0.001,
         "Power publishes only confirmed setpoint");

  transport->gateNextRead(0x26);
  const auto enable = observePendingCommand(edfa, *transport, "Enable",
      [&edfa](std::string& e) { return edfa.setOutputEnabled(true, e); });
  expect(enable.succeeded && !enable.pending.output_enabled && edfa.status().output_enabled,
         "Output enable is not published before acknowledgement");

  transport->gateNextRead(0x00);
  const auto alarm = observePendingCommand(edfa, *transport, "Alarm refresh",
      [&edfa](std::string& e) { return edfa.resetAlarm(e); });
  expect(alarm.succeeded, "Alarm refresh remains available");

  transport->gateNextRead(0x26);
  const auto disable = observePendingCommand(edfa, *transport, "Emergency off",
      [&edfa](std::string& e) { return edfa.emergencyOff(e); });
  expect(disable.succeeded && disable.pending.output_enabled && !edfa.status().output_enabled,
         "Output disable remains acknowledgement-based");

  expect(edfa.setOutputEnabled(true, error), "Output re-enables before disconnect test");
  transport->gateNextRead(0x26);
  const auto disconnect = observePendingCommand(edfa, *transport, "Disconnect",
      [&edfa](std::string&) { edfa.disconnect(); return true; });
  expect(disconnect.pending.output_enabled && !edfa.status().device.connected &&
             !edfa.status().telemetry_valid, "Disconnect invalidates the published connection");
}

void testPollFailureAndCommandSerialization() {
  auto transport = std::make_shared<GatedEdfaTransport>();
  fmcw::EdfaSerialController edfa(transport);
  std::string error;
  expect(edfa.configure(testConfig(), error) && edfa.connect(error), "Failure fixture connects");
  const auto initial_timestamp = edfa.status().telemetry_timestamp_ns;
  transport->current_ma = 1200;
  transport->gateNextRead(0x25, true);
  const auto poll = observePendingCommand(edfa, *transport, "Poll timeout",
      [&edfa](std::string& e) { return edfa.pollStatus(e); });
  const auto failed = edfa.status();
  expect(!poll.succeeded && !poll.error.empty() && !failed.telemetry_valid && !failed.device.ready &&
             failed.measured_current_ma == 1000.0,
         "Failed full poll preserves last confirmed readings and invalidates telemetry");
  expect(failed.telemetry_timestamp_ns == initial_timestamp,
         "A failed full poll cannot advance the last confirmed telemetry timestamp");
  expect(edfa.pollStatus(error) && edfa.status().telemetry_valid &&
             edfa.status().measured_current_ma == 1200.0, "A later poll recovers telemetry");

  transport->gateNextRead(0x00);
  auto polling = std::async(std::launch::async, [&edfa] {
    std::string e;
    return edfa.pollStatus(e);
  });
  expect(transport->waitForBlockedRead(), "Serialization poll reaches UART");
  const auto writes = transport->writes.load();
  std::promise<void> mode_entered;
  auto mode_entered_future = mode_entered.get_future();
  auto setting = std::async(std::launch::async, [&edfa, &mode_entered] {
    std::string e;
    mode_entered.set_value();
    return edfa.setControlMode(fmcw::EdfaControlMode::Acc, e);
  });
  mode_entered_future.wait();
  expect(setting.wait_for(100ms) == std::future_status::timeout && transport->writes == writes,
         "Control commands cannot interleave bytes with an in-flight status poll");
  transport->releaseRead();
  expect(polling.get() && setting.get() && edfa.status().control_mode == fmcw::EdfaControlMode::Acc,
         "Queued command confirms and publishes its result after the poll");
  const auto writes_before_status = transport->writes.load();
  for (int i = 0; i < 100; ++i) {
    (void)edfa.status();
  }
  expect(transport->writes == writes_before_status, "Status reads do not perform serial I/O");
}

void testAcknowledgementAndTelemetryFailure() {
  auto transport = std::make_shared<GatedEdfaTransport>();
  fmcw::EdfaSerialController edfa(transport);
  std::string error;
  expect(edfa.configure(testConfig(), error), "Partial connection fixture configures");
  transport->gateNextRead(0x25, true);
  const auto connection = observePendingCommand(edfa, *transport, "Connection timeout",
      [&edfa](std::string& e) { return edfa.connect(e); });
  expect(!connection.succeeded && !edfa.status().device.connected &&
             !edfa.status().telemetry_valid && edfa.status().telemetry_timestamp_ns == 0U,
         "Incomplete connection does not expose partially queried readings");
  expect(edfa.connect(error), "Connection recovers after timeout");

  transport->gateNextRead(0x26, true);
  const auto rejected = observePendingCommand(edfa, *transport, "Activation timeout",
      [&edfa](std::string& e) { return edfa.setOutputEnabled(true, e); });
  expect(!rejected.succeeded && !edfa.status().output_enabled,
         "Unconfirmed activation never becomes a confirmed output state");

  const auto before_telemetry = edfa.status().telemetry_timestamp_ns;
  transport->gateNextRead(0x00, true);
  const auto enabled = observePendingCommand(edfa, *transport, "Post-activation telemetry timeout",
      [&edfa](std::string& e) { return edfa.setOutputEnabled(true, e); });
  expect(enabled.succeeded && enabled.pending.output_enabled && edfa.status().output_enabled &&
             !edfa.status().telemetry_valid &&
             edfa.status().telemetry_timestamp_ns == before_telemetry,
         "Confirmed activation is visible even if optional telemetry later times out");
  expect(edfa.emergencyOff(error), "Confirmed shutdown still works after failed telemetry");

  for (const auto mode : {fmcw::EdfaMode::None, fmcw::EdfaMode::Manual}) {
    auto config = testConfig();
    config.edfa.mode = mode;
    edfa.disconnect();
    const auto writes = transport->writes.load();
    expect(edfa.configure(config, error) && edfa.connect(error) && edfa.pollStatus(error) &&
               edfa.emergencyOff(error), "Bypass and manual modes still work without serial I/O");
    expect(transport->writes == writes && edfa.status().device.ready &&
               edfa.status().mode == mode && edfa.status().telemetry_timestamp_ns == 0U,
           "Bypass/manual status has no fabricated telemetry freshness");
  }
}

void testAcquisitionDuringPoll() {
  auto transport = std::make_shared<GatedEdfaTransport>();
  fmcw::EdfaSerialController edfa(transport);
  fmcw::FakeDigitizer digitizer;
  fmcw::FakeMcuController mcu;
  fmcw::AcquisitionSession session(digitizer, edfa, mcu);
  std::string error;
  expect(session.configure(testConfig(), 7, error) && session.connect(error) && session.start(error),
         "Acquisition uses real EDFA controller logic with simulated devices");
  transport->gateNextRead(0x00, true);
  auto polling = std::async(std::launch::async, [&edfa] {
    std::string e;
    return edfa.pollStatus(e);
  });
  expect(transport->waitForBlockedRead(), "Acquisition test poll reaches gated read");
  auto acquisition = std::async(std::launch::async, [&session] {
    for (int i = 0; i < 8; ++i) {
      fmcw::RawFrameBatchPtr batch;
      std::string e;
      const auto result = session.waitForBatch(batch, 100ms, e);
      if (result != fmcw::FrameWaitResult::FrameReady) {
        return std::string("batch ") + std::to_string(i) + ": wait result " +
            std::to_string(static_cast<int>(result)) + " " + e;
      }
      if (!batch || batch->records.size() != 4U ||
          !batch->records.front().metadata.optical_state.edfa_output_enabled) {
        return std::string("batch ") + std::to_string(i) + ": invalid batch/optical metadata";
      }
    }
    return std::string{};
  });
  expect(acquisition.wait_for(500ms) == std::future_status::ready,
         "Eight acquisition batches finish while the EDFA response is still withheld");
  transport->releaseRead();
  const auto acquisition_error = acquisition.get();
  expect(acquisition_error.empty(),
         "Acquisition preserves batch and confirmed optical metadata: " + acquisition_error);
  expect(!polling.get() && !edfa.status().telemetry_valid, "Poll timeout still reports failure");
  expect(session.stop(error), "Session stops after the injected EDFA timeout");
}

}  // namespace

int main() {
  testStatusDuringCommands();
  testPollFailureAndCommandSerialization();
  testAcknowledgementAndTelemetryFailure();
  testAcquisitionDuringPoll();
  if (failures == 0) {
    std::cout << "All EDFA concurrency tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
