#include "apps/common/application_controller.h"

#include "core/acquisition_session.h"
#include "core/app_version.h"
#include "core/config_profile.h"
#include "drivers/simulator/fake_digitizer.h"
#include "drivers/simulator/fake_edfa.h"
#include "drivers/simulator/fake_mcu.h"
#include "processing/fft_backends.h"
#include "processing/processing_service.h"
#include "storage/async_storage_service.h"

#include <QDateTime>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <utility>
#include <vector>

namespace fmcw {
namespace {

std::uint64_t utcNowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

}  // namespace

class RuntimeWorker final : public QObject {
  Q_OBJECT

 public:
  explicit RuntimeWorker(QString platform_name)
      : platform_name_(std::move(platform_name)), session_(digitizer_, edfa_, mcu_) {}

 public slots:
  void initialize() {
    acquisition_timer_ = new QTimer(this);
    acquisition_timer_->setTimerType(Qt::PreciseTimer);
    acquisition_timer_->setInterval(8);
    connect(acquisition_timer_, &QTimer::timeout, this, &RuntimeWorker::acquireOne);
    emitLog("INFO", "Runtime", "Simulator runtime worker ready");
    publishStatus("Disconnected. Apply a profile or connect the simulator.");
  }

  void configure(SystemConfig config) {
    QString error;
    if (!configureRuntime(config, error)) {
      fail("Apply configuration", error);
      return;
    }
    emit commandCompleted("Apply configuration", "Configuration is valid and ready");
  }

  void connectRuntime(SystemConfig config) {
    QString error;
    if (!configureRuntime(config, error)) {
      fail("Connect", error);
      return;
    }
    std::string core_error;
    if (!session_.connect(core_error)) {
      fail("Connect", qString(core_error));
      return;
    }
    connected_ = true;
    state_ = OperationState::Ready;
    emitLog("INFO", "Device", "Digitizer, EDFA, and MCU simulator adapters connected");
    publishStatus("Simulator connected and ready");
    emit commandCompleted("Connect", "Simulator devices are ready");
  }

  void disconnectRuntime() {
    if (running_) {
      stopRuntime(false, "Disconnected by operator");
    }
    session_.disconnect();
    connected_ = false;
    state_ = configured_ ? OperationState::Configured : OperationState::Disconnected;
    emitLog("INFO", "Device", "Runtime devices disconnected");
    publishStatus("Disconnected");
    emit commandCompleted("Disconnect", "Runtime devices disconnected");
  }

  void startRuntime(SystemConfig config) {
    if (running_) {
      reject("Start", "Acquisition is already running");
      return;
    }

    QString error;
    if (!configureRuntime(config, error)) {
      fail("Start", error);
      return;
    }
    if (!connected_) {
      std::string core_error;
      if (!session_.connect(core_error)) {
        fail("Start", qString(core_error));
        return;
      }
      connected_ = true;
    }

    storage_.reset();
    if (config_.storage.raw_enabled || config_.storage.processed_enabled) {
      storage_ = std::make_unique<AsyncStorageService>();
      WriterOpenOptions options;
      const auto session_id = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
      options.session_directory = std::filesystem::path(config_.storage.output_directory) /
                                  session_id.toStdString();
      options.file_stem = "fmcw_lidar";
      options.session.session_id = session_id.toStdString();
      options.session.profile_id = config_.profile.id;
      options.session.platform = platform_name_.toStdString();
      options.session.application_version = versionString();
      options.session.start_timestamp_utc_ns = utcNowNs();
      options.session.config_snapshot_json = ConfigProfileCodec::toJsonSnapshot(config_);
      options.raw_stream.channel = config_.digitizer.channel;
      options.raw_stream.sample_rate_hz = config_.digitizer.sample_rate_hz;
      options.raw_stream.record_length = config_.digitizer.sample_point;
      options.raw_enabled = config_.storage.raw_enabled;
      options.processed_enabled = config_.storage.processed_enabled;
      options.queue_capacity = config_.storage.queue_capacity;
      options.overflow_policy = config_.storage.overflow_policy;
      options.split_file_size_gb = config_.storage.split_file_size_gb;
      options.flush_interval_frames = config_.storage.flush_interval_frames;
      std::string core_error;
      if (!storage_->start(options, core_error)) {
        storage_.reset();
        fail("Start", qString(core_error));
        return;
      }
    }

    processing_->setProcessedFrameCallback([this](ProcessedFramePtr frame) {
      if (storage_ == nullptr || !config_.storage.processed_enabled) {
        return;
      }
      std::string error_message;
      const auto result = storage_->enqueueProcessed(std::move(frame), error_message);
      if (result != EnqueueResult::Accepted) {
        storage_failure_pending_ = true;
      }
    });

    std::string core_error;
    if (!processing_->start(core_error)) {
      stopStorage("Processing failed to start");
      fail("Start", qString(core_error));
      return;
    }

    state_ = OperationState::Preview;
    publishStatus("Starting devices in EDFA, digitizer, MCU order...");
    if (!session_.start(core_error)) {
      const auto start_error = core_error;
      processing_->requestStop("Device start failed");
      processing_->waitUntilStopped(core_error);
      stopStorage("Device start failed");
      fail("Start", qString(start_error));
      return;
    }

    running_ = true;
    recording_ = config_.storage.raw_enabled || config_.storage.processed_enabled;
    storage_failure_pending_ = false;
    snapshot_divider_ = 0;
    state_ = recording_ ? OperationState::Recording : OperationState::Acquiring;
    acquisition_timer_->start();
    emitLog("INFO", "Acquisition", "Global START completed; full-period up-triggered frames are active");
    if (config_.udp.enabled) {
      emitLog("WARNING", "UDP", "Endpoint is configured; network sender activation is scheduled for Phase 6");
    }
    publishStatus(recording_ ? "Acquiring and recording" : "Acquiring");
    emit commandCompleted("Start", "Acquisition started");
  }

  void stopRuntimeCommand() {
    if (!running_) {
      publishStatus("Already stopped");
      emit commandCompleted("Stop", "Acquisition is not running");
      return;
    }
    stopRuntime(false, "Stopped by operator");
    emit commandCompleted("Stop", "Acquisition stopped cleanly");
  }

  void emergencyStopRuntime() {
    if (acquisition_timer_ != nullptr) {
      acquisition_timer_->stop();
    }
    std::string error;
    session_.emergencyStop(error);
    stopProcessing("Emergency stop");
    stopStorage("Emergency stop");
    running_ = false;
    recording_ = false;
    state_ = OperationState::Error;
    emitLog("CRITICAL", "Safety", "Emergency stop asserted; MCU trigger, digitizer, and EDFA output are off");
    publishStatus(error.empty() ? "Emergency stop asserted" : qString(error));
    emit commandCompleted("Emergency stop", "All runtime outputs stopped");
  }

  void updateProcessingRuntime(ProcessingConfig config) {
    if (!running_ || processing_ == nullptr) {
      reject("Processing update", "Runtime processing updates require an active acquisition");
      return;
    }
    std::string error;
    ++processing_revision_;
    if (!processing_->updateRuntimeConfig(config, processing_revision_, error)) {
      --processing_revision_;
      reject("Processing update", qString(error));
      return;
    }
    config_.processing = config;
    emitLog("INFO", "Processing", "Peak and preprocessing settings queued for the next frame boundary");
    publishStatus("Runtime processing settings updated");
    emit commandCompleted("Processing update", "Settings apply on the next processed frame");
  }

  void setEdfaOutputRuntime(bool enabled) {
    std::string error;
    if (!edfa_.setOutputEnabled(enabled, error)) {
      reject("EDFA output", qString(error));
      return;
    }
    emitLog(enabled ? "WARNING" : "INFO", "EDFA",
            enabled ? "EDFA output enabled independently of acquisition" : "EDFA output disabled");
    publishStatus(enabled ? "EDFA output enabled" : "EDFA output disabled");
    emit commandCompleted("EDFA output", enabled ? "Output enabled" : "Output disabled");
  }

  void uploadMcuWaveformRuntime() {
    if (!connected_) {
      reject("MCU waveform", "Connect the runtime before uploading a waveform");
      return;
    }
    if (!config_.mcu.enabled) {
      emit commandCompleted("MCU waveform", "MCU is disabled; bypass remains ready");
      return;
    }
    const auto count = std::min<std::uint32_t>(config_.scan.x_pixel_count, 15000U);
    std::vector<McuWaveformFrame> frames(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto code = static_cast<std::uint16_t>(count <= 1U ? 0U : (index * 65535U) / (count - 1U));
      frames[index].a = code;
      frames[index].b = static_cast<std::uint16_t>(65535U - code);
      frames[index].trigger = true;
    }
    std::string error;
    if (!mcu_.uploadWaveform(frames, error)) {
      reject("MCU waveform", qString(error));
      return;
    }
    emitLog("INFO", "MCU", QString("Uploaded %1 scan waveform points").arg(count));
    publishStatus("MCU waveform loaded");
    emit commandCompleted("MCU waveform", QString("%1 points uploaded").arg(count));
  }

  void captureSegmentationSnapshotRuntime() {
    if (processing_ == nullptr) {
      reject("Capture snapshot", "No processing service is configured");
      return;
    }
    const auto snapshot = processing_->snapshots().latestWaveform();
    if (snapshot == nullptr) {
      reject("Capture snapshot", "No frame is available yet");
      return;
    }
    emit segmentationSnapshotReady(snapshot);
    emit commandCompleted("Capture snapshot", QString("Frame %1 frozen").arg(snapshot->frame_id));
  }

  void shutdown() {
    if (running_) {
      stopRuntime(true, "Application shutdown");
    }
    session_.disconnect();
    connected_ = false;
  }

 signals:
  void statusChanged(fmcw::RuntimeStatus status);
  void waveformReady(fmcw::WaveformSnapshotPtr snapshot);
  void fftReady(fmcw::FftSnapshotPtr snapshot);
  void scanLineReady(fmcw::ScanLineSnapshotPtr snapshot);
  void bscanReady(fmcw::BScanSnapshotPtr snapshot);
  void segmentationSnapshotReady(fmcw::WaveformSnapshotPtr snapshot);
  void logMessage(QString level, QString source, QString message);
  void commandFailed(QString command, QString message);
  void commandCompleted(QString command, QString message);

 private:
  bool configureRuntime(const SystemConfig& config, QString& error) {
    if (running_) {
      error = "Stop acquisition before applying hardware or FFT backend changes";
      return false;
    }

    const bool reconnect = connected_;
    if (reconnect) {
      session_.disconnect();
      connected_ = false;
    }
    processing_.reset();
    storage_.reset();

    auto backend = createFftBackend(config.processing.fft_backend);
    processing_ = std::make_unique<ProcessingService>(std::move(backend));
    std::string core_error;
    const auto next_revision = config_revision_ + 1U;
    if (!processing_->configure(config, next_revision, core_error)) {
      processing_.reset();
      error = qString(core_error);
      return false;
    }
    if (!session_.configure(config, next_revision, core_error)) {
      processing_.reset();
      error = qString(core_error);
      return false;
    }

    config_ = config;
    config_revision_ = next_revision;
    processing_revision_ = next_revision;
    configured_ = true;
    state_ = OperationState::Configured;

    if (reconnect) {
      if (!session_.connect(core_error)) {
        error = qString(core_error);
        return false;
      }
      connected_ = true;
      state_ = OperationState::Ready;
    }
    publishStatus(reconnect ? "Configuration applied and devices reconnected" : "Configuration applied");
    emitLog("INFO", "Configuration",
            QString("Applied profile '%1' as revision %2")
                .arg(qString(config_.profile.name))
                .arg(config_revision_));
    error.clear();
    return true;
  }

  void acquireOne() {
    if (!running_ || processing_ == nullptr) {
      return;
    }
    RawFrame frame;
    std::string error;
    const auto wait_result = session_.waitForFrame(frame, std::chrono::milliseconds(1), error);
    if (wait_result == FrameWaitResult::Timeout) {
      publishStatus("Waiting for trigger");
      return;
    }
    if (wait_result != FrameWaitResult::FrameReady) {
      stopRuntime(false, error.empty() ? QStringLiteral("Acquisition stopped") : qString(error));
      return;
    }

    auto shared_frame = std::make_shared<const RawFrame>(std::move(frame));
    if (storage_ != nullptr && config_.storage.raw_enabled) {
      const auto storage_result = storage_->enqueueRaw(shared_frame, error);
      if (storage_result != EnqueueResult::Accepted) {
        stopRuntime(false, error.empty() ? QStringLiteral("Raw storage queue stopped") : qString(error));
        return;
      }
    }
    const auto processing_result = processing_->enqueue(shared_frame, error);
    if (processing_result != ProcessingEnqueueResult::Accepted) {
      stopRuntime(false, error.empty() ? QStringLiteral("Processing queue stopped") : qString(error));
      return;
    }

    if (storage_failure_pending_) {
      stopRuntime(false, "Processed storage queue stopped");
      return;
    }
    const auto processing_status = processing_->status();
    if (processing_status.stop_requested) {
      stopRuntime(false, qString(processing_status.stop_reason));
      return;
    }
    if (storage_ != nullptr && storage_->status().stop_requested) {
      stopRuntime(false, qString(storage_->status().stop_reason));
      return;
    }

    ++snapshot_divider_;
    const auto divisor = std::max(1, static_cast<int>(125.0 / std::clamp(config_.ui.plot_update_hz, 1.0, 60.0)));
    if ((snapshot_divider_ % divisor) == 0) {
      publishSnapshots();
      publishStatus(recording_ ? "Acquiring and recording" : "Acquiring");
    }
  }

  void publishSnapshots() {
    const auto waveform = processing_->snapshots().latestWaveform();
    const auto fft = processing_->snapshots().latestFft();
    const auto line = processing_->snapshots().latestScanLine();
    const auto bscan = processing_->snapshots().latestBScan();
    if (waveform != nullptr) {
      emit waveformReady(waveform);
    }
    if (fft != nullptr) {
      emit fftReady(fft);
    }
    if (line != nullptr) {
      emit scanLineReady(line);
    }
    if (bscan != nullptr) {
      emit bscanReady(bscan);
    }
  }

  void stopRuntime(bool emergency, const QString& reason) {
    if (acquisition_timer_ != nullptr) {
      acquisition_timer_->stop();
    }
    state_ = OperationState::Stopping;
    publishStatus("Stopping MCU trigger, digitizer, processing, storage, and EDFA...");
    std::string error;
    if (emergency) {
      session_.emergencyStop(error);
    } else {
      session_.stop(error);
    }
    stopProcessing(reason);
    stopStorage(reason);
    running_ = false;
    recording_ = false;
    state_ = connected_ ? OperationState::Ready : OperationState::Configured;
    emitLog(error.empty() ? "INFO" : "ERROR", "Acquisition",
            error.empty() ? QString("Stopped: %1").arg(reason) : qString(error));
    publishSnapshots();
    publishStatus(error.empty() ? reason : qString(error));
  }

  void stopProcessing(const QString& reason) {
    if (processing_ == nullptr) {
      return;
    }
    processing_->requestStop(reason.toStdString());
    std::string error;
    processing_->waitUntilStopped(error);
  }

  void stopStorage(const QString& reason) {
    if (storage_ == nullptr) {
      return;
    }
    storage_->requestStop(reason.toStdString());
    std::string error;
    storage_->waitUntilStopped(error);
  }

  void publishStatus(const QString& detail) {
    RuntimeStatus status;
    status.state = state_;
    status.configured = configured_;
    status.connected = connected_;
    status.running = running_;
    status.recording = recording_;
    status.config_revision = config_revision_;
    status.processing_revision = processing_revision_;
    status.detail = detail;
    const auto telemetry = session_.telemetry();
    status.digitizer_ready = telemetry.digitizer.device.ready;
    status.edfa_ready = telemetry.edfa.device.ready;
    status.edfa_bypassed = telemetry.edfa.bypassed;
    status.edfa_output_enabled = telemetry.edfa.output_enabled;
    status.mcu_ready = telemetry.mcu.device.ready;
    status.mcu_bypassed = !config_.mcu.enabled;
    status.mcu_waveform_loaded = telemetry.mcu.waveform_points > 0U;
    status.frames_received = telemetry.digitizer.frames_received;
    if (processing_ != nullptr) {
      const auto processing_status = processing_->status();
      status.frames_processed = processing_status.frames_processed;
      status.processing_queue_size = processing_status.queue_size;
      status.processing_queue_capacity = processing_status.queue_capacity;
      status.processing_latency_ms = processing_status.average_latency_ms;
      status.backend_name = qString(processing_status.backend_name);
      status.processing_revision = processing_status.processing_config_revision;
    }
    if (storage_ != nullptr) {
      const auto storage_status = storage_->status();
      status.frames_written = storage_status.raw_writer.frames_written +
                              storage_status.processed_writer.frames_written;
      status.storage_queue_size = storage_status.queue_size;
      status.storage_queue_capacity = storage_status.queue_capacity;
      status.storage_throughput_mbps = storage_status.raw_writer.throughput_mbps +
                                       storage_status.processed_writer.throughput_mbps;
    }
    emit statusChanged(status);
  }

  void fail(const QString& command, const QString& message) {
    state_ = OperationState::Error;
    emitLog("ERROR", command, message);
    publishStatus(message);
    emit commandFailed(command, message);
  }

  void reject(const QString& command, const QString& message) {
    emitLog("WARNING", command, message);
    publishStatus(message);
    emit commandFailed(command, message);
  }

  void emitLog(const QString& level, const QString& source, const QString& message) {
    emit logMessage(level, source, message);
  }

  QString platform_name_;
  QTimer* acquisition_timer_ = nullptr;
  FakeDigitizer digitizer_;
  FakeEdfaController edfa_;
  FakeMcuController mcu_;
  AcquisitionSession session_;
  std::unique_ptr<ProcessingService> processing_;
  std::unique_ptr<AsyncStorageService> storage_;
  SystemConfig config_;
  OperationState state_ = OperationState::Disconnected;
  std::uint64_t config_revision_ = 0;
  std::uint64_t processing_revision_ = 0;
  int snapshot_divider_ = 0;
  bool configured_ = false;
  bool connected_ = false;
  bool running_ = false;
  bool recording_ = false;
  std::atomic_bool storage_failure_pending_{false};
};

ApplicationController::ApplicationController(QString platform_name, QObject* parent) : QObject(parent) {
  qRegisterMetaType<RuntimeStatus>();
  qRegisterMetaType<WaveformSnapshotPtr>();
  qRegisterMetaType<FftSnapshotPtr>();
  qRegisterMetaType<ScanLineSnapshotPtr>();
  qRegisterMetaType<BScanSnapshotPtr>();

  worker_ = new RuntimeWorker(std::move(platform_name));
  worker_->moveToThread(&runtime_thread_);
  connect(&runtime_thread_, &QThread::started, worker_, &RuntimeWorker::initialize);
  connect(&runtime_thread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(worker_, &RuntimeWorker::statusChanged, this, &ApplicationController::statusChanged);
  connect(worker_, &RuntimeWorker::waveformReady, this, &ApplicationController::waveformReady);
  connect(worker_, &RuntimeWorker::fftReady, this, &ApplicationController::fftReady);
  connect(worker_, &RuntimeWorker::scanLineReady, this, &ApplicationController::scanLineReady);
  connect(worker_, &RuntimeWorker::bscanReady, this, &ApplicationController::bscanReady);
  connect(worker_, &RuntimeWorker::segmentationSnapshotReady, this,
          &ApplicationController::segmentationSnapshotReady);
  connect(worker_, &RuntimeWorker::logMessage, this, &ApplicationController::logMessage);
  connect(worker_, &RuntimeWorker::commandFailed, this, &ApplicationController::commandFailed);
  connect(worker_, &RuntimeWorker::commandCompleted, this, &ApplicationController::commandCompleted);
  runtime_thread_.setObjectName("FMCW runtime");
  runtime_thread_.start();
}

ApplicationController::~ApplicationController() {
  if (worker_ != nullptr && runtime_thread_.isRunning()) {
    QMetaObject::invokeMethod(worker_, &RuntimeWorker::shutdown, Qt::BlockingQueuedConnection);
  }
  runtime_thread_.quit();
  runtime_thread_.wait();
}

void ApplicationController::applyConfig(const SystemConfig& config) {
  QMetaObject::invokeMethod(worker_, [worker = worker_, config] { worker->configure(config); }, Qt::QueuedConnection);
}

void ApplicationController::connectSystem(const SystemConfig& config) {
  QMetaObject::invokeMethod(worker_, [worker = worker_, config] { worker->connectRuntime(config); },
                            Qt::QueuedConnection);
}

void ApplicationController::disconnectSystem() {
  QMetaObject::invokeMethod(worker_, &RuntimeWorker::disconnectRuntime, Qt::QueuedConnection);
}

void ApplicationController::startSystem(const SystemConfig& config) {
  QMetaObject::invokeMethod(worker_, [worker = worker_, config] { worker->startRuntime(config); },
                            Qt::QueuedConnection);
}

void ApplicationController::stopSystem() {
  QMetaObject::invokeMethod(worker_, &RuntimeWorker::stopRuntimeCommand, Qt::QueuedConnection);
}

void ApplicationController::emergencyStop() {
  QMetaObject::invokeMethod(worker_, &RuntimeWorker::emergencyStopRuntime, Qt::QueuedConnection);
}

void ApplicationController::updateProcessing(const ProcessingConfig& config) {
  QMetaObject::invokeMethod(worker_, [worker = worker_, config] { worker->updateProcessingRuntime(config); },
                            Qt::QueuedConnection);
}

void ApplicationController::setEdfaOutput(bool enabled) {
  QMetaObject::invokeMethod(worker_, [worker = worker_, enabled] { worker->setEdfaOutputRuntime(enabled); },
                            Qt::QueuedConnection);
}

void ApplicationController::uploadMcuWaveform() {
  QMetaObject::invokeMethod(worker_, &RuntimeWorker::uploadMcuWaveformRuntime, Qt::QueuedConnection);
}

void ApplicationController::captureSegmentationSnapshot() {
  QMetaObject::invokeMethod(worker_, &RuntimeWorker::captureSegmentationSnapshotRuntime, Qt::QueuedConnection);
}

}  // namespace fmcw

#include "application_controller.moc"
