#include "application/application_controller.h"
#include "drivers/simulator/fake_digitizer.h"
#include "drivers/simulator/fake_edfa.h"
#include "drivers/simulator/fake_mcu.h"
#include "drivers/replay/replay_digitizer.h"
#include "storage/binary_storage.h"
#include "core/config/config_profile.h"
#include "processing/fft_backends.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <atomic>
#include <iostream>
#include <thread>

using namespace fmcw;
using namespace std::chrono_literals;
namespace {
int failures = 0;
void expect(bool value, const char* message) { if (!value) { ++failures; std::cerr << message << '\n'; } }
bool until(const std::function<bool()>& ready, int timeout = 4000) {
  QElapsedTimer timer; timer.start();
  while (!ready() && timer.elapsed() < timeout) { QCoreApplication::processEvents(); std::this_thread::sleep_for(1ms); }
  return ready();
}
struct Faults {
  std::atomic_bool fail_connect{false}, output{false}, uploading{false}, hold_upload{false};
  std::atomic_bool fail_off{false};
};
class Edfa final : public IEdfaController {
 public:
  explicit Edfa(std::shared_ptr<Faults> faults) : faults_(std::move(faults)) {}
  std::string name() const override { return fake_.name(); }
  EdfaStatus status() const override { return fake_.status(); }
  bool configure(const SystemConfig& c, std::string& e) override { return fake_.configure(c,e); }
  bool connect(std::string& e) override {
    if (faults_->fail_connect.exchange(false)) { e = "Injected connect failure"; return false; }
    return fake_.connect(e);
  }
  void disconnect() override { fake_.disconnect(); }
  bool pollStatus(std::string& e) override { return fake_.pollStatus(e); }
  bool setControlMode(EdfaControlMode m, std::string& e) override { return fake_.setControlMode(m,e); }
  bool setOutputSetpoint(const OpticalPowerSetpoint& p, std::string& e) override { return fake_.setOutputSetpoint(p,e); }
  bool setOutputEnabled(bool value, std::string& e) override {
    if (!value && faults_->fail_off.exchange(false)) { e="Injected off ACK failure"; return false; }
    const auto success = fake_.setOutputEnabled(value,e); if (success) faults_->output = value; return success;
  }
  bool resetAlarm(std::string& e) override { return fake_.resetAlarm(e); }
  bool emergencyOff(std::string& e) override { faults_->output = false; return fake_.emergencyOff(e); }
 private:
  FakeEdfaController fake_;
  std::shared_ptr<Faults> faults_;
};
class Mcu final : public IMcuController {
 public:
  explicit Mcu(std::shared_ptr<Faults> faults) : faults_(std::move(faults)) {}
  std::string name() const override { return fake_.name(); }
  McuStatus status() const override { return fake_.status(); }
  bool configure(const SystemConfig& c, std::string& e) override { return fake_.configure(c,e); }
  bool connect(std::string& e) override { return fake_.connect(e); }
  void disconnect() override { fake_.disconnect(); }
  bool uploadWaveform(const std::vector<McuWaveformFrame>& frames, std::string& error,
      const McuUploadProgressCallback& progress, const CancellationCheck& cancelled) override {
    faults_->uploading = true;
    while (faults_->hold_upload && !isCancelled(cancelled)) { std::this_thread::sleep_for(1ms); }
    faults_->uploading = false;
    return fake_.uploadWaveform(frames,error,progress,cancelled);
  }
  McuWaveformSnapshotPtr loadedWaveform() const override { return fake_.loadedWaveform(); }
  bool startScan(std::string& e) override { return fake_.startScan(e); }
  bool stopScan(std::string& e) override { return fake_.stopScan(e); }
  bool emergencyStop(std::string& e) override { return fake_.emergencyStop(e); }
 private:
  FakeMcuController fake_;
  std::shared_ptr<Faults> faults_;
};
class FailedStorage final : public IStorageService {
 public:
  bool start(const WriterOpenOptions&, std::string& e) override { e.clear(); return true; }
  EnqueueResult enqueueRawBatch(RawFrameBatchPtr, std::string&) override { return EnqueueResult::Accepted; }
  EnqueueResult enqueueRaw(RawFramePtr, std::string&) override { return EnqueueResult::Accepted; }
  EnqueueResult enqueuePointCloud(std::shared_ptr<const PointCloudSnapshot>, std::string&) override { return EnqueueResult::Accepted; }
  EnqueueResult enqueueProcessingEvent(ProcessingConfigEvent, std::string&) override { return EnqueueResult::Accepted; }
  void requestStop(std::string, bool) override {}
  bool waitUntilStopped(std::string& e) override { e = "Injected finalize failure"; return false; }
  StorageStatus status() const override { return {}; }
};
RuntimeDependencies dependencies(const std::shared_ptr<Faults>& faults, bool fail_storage) {
  RuntimeDependencies result;
  result.adapters = [faults](AcquisitionSource source) {
    std::unique_ptr<IDigitizer> digitizer;
    if (source==AcquisitionSource::Replay) digitizer=std::make_unique<ReplayDigitizer>();
    else digitizer=std::make_unique<FakeDigitizer>();
    return RuntimeAdapters{std::move(digitizer), std::make_unique<Edfa>(faults),
      std::make_unique<Mcu>(faults), "Test adapters"};
  };
  if (fail_storage) result.storage = [] { return std::make_unique<FailedStorage>(); };
  return result;
}
struct Harness {
  std::shared_ptr<Faults> faults = std::make_shared<Faults>();
  ApplicationController app;
  RuntimeStatus status;
  QString failed, completed, detail;
  explicit Harness(bool storage = false) : app("Test", dependencies(faults, storage)) {
    QObject::connect(&app,&ApplicationController::statusChanged,[this](RuntimeStatus s) { status = std::move(s); });
    QObject::connect(&app,&ApplicationController::commandFailed,[this](QString c, QString d) { failed=c; detail=d; });
    QObject::connect(&app,&ApplicationController::commandCompleted,[this](QString c, QString) { completed=c; });
  }
  void clear() { failed.clear(); completed.clear(); detail.clear(); }
};
SystemConfig config() {
  SystemConfig c;
  c.processing.fft_backend = FftwBackend::available() ? FftBackendKind::Fftw : FftBackendKind::Cuda;
  c.digitizer.records_per_buffer = c.digitizer.a_scan_count = c.scan.x_pixel_count = 4;
  c.digitizer.b_scan_count = c.scan.y_line_count = 2;
  return c;
}
void testLifecycle() {
  Harness h;
  auto c = config(); h.faults->fail_connect = true;
  h.app.connectSystem(c);
  expect(until([&] { return h.failed == "Connect"; }), "Connection failure is reported");
  h.clear(); h.app.connectSystem(c);
  expect(until([&] { return h.status.connected; }), "Connection recovers through actual runtime");
  auto invalid = c; invalid.digitizer.records_per_buffer = 0;
  const auto revision = h.status.config_revision;
  h.app.applyConfig(invalid);
  expect(until([&] { return h.failed == "Apply configuration"; }), "Invalid Apply rejected");
  expect(h.status.connected && h.status.config_revision == revision, "Invalid Apply preserves connected setup");
  for (int i=0;i<2;++i) {
    h.clear(); h.app.startSystem();
    expect(until([&] { return h.status.running; }), "Runtime Start succeeds");
    h.app.stopSystem();
    expect(until([&] { return h.completed == "Stop"; }), "Runtime Stop succeeds");
    expect(h.failed.isEmpty(), "Normal stop has no failure");
  }
}
void testStopFailure() {
  Harness h(true); auto c=config(); c.storage.processed_enabled=true;
  h.app.connectSystem(c); expect(until([&] { return h.status.connected; }), "Storage fixture connects");
  h.app.startSystem(); expect(until([&] { return h.status.running; }), "Storage fixture starts");
  h.clear(); h.app.stopSystem();
  expect(until([&] { return h.failed == "Stop"; }), "Finalize failure reaches Stop signal");
  expect(h.detail.contains("Injected finalize failure") && h.completed != "Stop", "No false Stop success");
}
void testWarmupCancel() {
  Harness h; auto c=config(); c.edfa.mode=EdfaMode::Controlled; c.edfa.port="test"; c.edfa.warmup_delay_ms=10000;
  h.app.connectSystem(c); expect(until([&] { return h.status.connected; }), "Warmup fixture connects");
  h.app.startSystem(); expect(until([&] { return h.faults->output.load(); }), "Warmup reached enabled output");
  h.app.emergencyStop();
  expect(until([&] { return !h.faults->output && h.failed == "Start"; }, 2000), "Stop interrupts ten-second warmup");
}
void testHardwareStopFailure() {
  Harness h; auto c=config(); c.edfa.mode=EdfaMode::Controlled; c.edfa.port="test"; c.edfa.warmup_delay_ms=0;
  h.app.connectSystem(c); expect(until([&] { return h.status.connected; }), "Off failure fixture connects");
  h.app.startSystem(); expect(until([&] { return h.status.running; }), "Off failure fixture starts");
  h.clear(); h.faults->fail_off=true; h.app.stopSystem();
  expect(until([&] { return h.failed=="Stop"; }), "Unconfirmed device off reaches Stop failure");
  expect(h.detail.contains("Injected off ACK failure") && h.completed!="Stop",
         "Unconfirmed hardware output is never reported as clean Stop");
}
void testNaturalReplayEnd() {
  auto c=config();
  const auto directory=std::filesystem::temp_directory_path() / ("fmcw_runtime_eof_"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  WriterOpenOptions options;
  options.session_directory=directory; options.file_stem="eof";
  options.preallocate_raw_parts=false; options.minimum_free_space_bytes=0;
  options.raw_stream.format_version=kRawFrameBatchFormatVersion;
  options.raw_stream.record_length=c.digitizer.sample_point;
  options.raw_stream.sample_rate_hz=c.digitizer.sample_rate_hz;
  options.raw_stream.records_per_buffer=4;
  options.session.config_snapshot_yaml=ConfigProfileCodec::toYaml(c);
  std::string error;
  BinaryRawFrameWriter writer;
  expect(writer.open(options,error), "EOF recording opens");
  FakeDigitizer source;
  expect(source.configure(c,error)&&source.connect(error)&&source.start(error), "EOF data source starts");
  for (int i=0;i<4;++i) {
    MutableRawFrameBatchPtr batch;
    expect(source.waitForBatch(batch,100ms,error)==FrameWaitResult::FrameReady &&
           writer.writeBatch(*batch,error), "EOF fixture writes batch");
  }
  source.stop(error);
  expect(writer.finalize({0,"fixture complete",true},error), "EOF fixture finalizes");
  {
    Harness h;
    c.runtime.acquisition_source=AcquisitionSource::Replay;
    c.runtime.replay_file=(directory/"eof.raw.0000.bin").string();
    h.app.connectSystem(c); expect(until([&] { return h.status.connected; }), "EOF runtime connects");
    h.app.startSystem();
    expect(until([&] { return h.status.frames_processed==16 && !h.status.running; }),
           "Natural EOF drains all records through the actual runtime");
    expect(h.failed.isEmpty(), "Natural EOF is not reported as a failure");
  }
  std::error_code cleanup;
  std::filesystem::remove_all(directory,cleanup);
  expect(!cleanup, "EOF fixture released all file handles");
}
void testUploadAndRetention() {
  Harness h; auto c=config(); c.mcu.enabled=true; c.mcu.port="test";
  c.mcu.waveform_source=McuWaveformSource::GeneratedRaster;
  h.app.connectSystem(c); expect(until([&] { return h.status.connected; }), "MCU fixture connects");
  h.faults->hold_upload=true; h.app.uploadMcuWaveform();
  expect(until([&] { return h.faults->uploading.load(); }), "Upload is in progress");
  h.app.startSystem(); expect(h.failed=="Start", "Start is rejected immediately during upload");
  h.app.stopSystem(); expect(until([&] { return !h.faults->uploading && h.failed=="MCU waveform"; }), "Stop cancels upload");
  h.faults->hold_upload=false; h.clear(); h.app.uploadMcuWaveform();
  expect(until([&] { return h.completed=="MCU waveform"; }), "New upload succeeds after cancellation");
  c.processing.peak_threshold_db=-50; h.clear(); h.app.applyConfig(c);
  expect(until([&] { return h.completed=="Apply configuration"; }), "Non-scanner Apply completes");
  expect(until([&] { return h.status.mcu_waveform_loaded; }), "Waveform is retained by actual runtime");
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc,argv);
  if (!FftwBackend::available() && !CudaFftBackend::available()) return 77;
  testLifecycle(); testStopFailure(); testWarmupCancel(); testHardwareStopFailure(); testUploadAndRetention();
  testNaturalReplayEnd();
  return failures ? 1 : 0;
}
