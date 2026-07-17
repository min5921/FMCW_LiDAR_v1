#include "apps/common/application_controller.h"

#include "core/acquisition_session.h"
#include "core/app_version.h"
#include "core/config_profile.h"
#include "core/continuous_acquisition_worker.h"
#include "drivers/mcu/mcu_protocol.h"
#include "drivers/runtime_adapter_factory.h"
#include "network/udp_sender_service.h"
#include "processing/fft_backends.h"
#include "processing/processing_service.h"
#include "storage/async_storage_service.h"
#include "storage/binary_storage.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <utility>
#include <vector>

namespace fmcw {
namespace {

constexpr qint64 kStatusUpdateIntervalMs = 100;

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
  explicit RuntimeWorker(QString platform_name) : platform_name_(std::move(platform_name)) {}

 public slots:
  void initialize() {
    ui_timer_ = new QTimer(this);
    ui_timer_->setTimerType(Qt::PreciseTimer);
    ui_timer_->setInterval(33);
    connect(ui_timer_, &QTimer::timeout, this, &RuntimeWorker::publishPeriodic);
    QString error;
    if (!selectRuntimeAdapters(AcquisitionSource::Simulator, error)) {
      fail("Runtime initialization", error);
      return;
    }
    emitLog("INFO", "Runtime", "Runtime adapter factory ready");
    publishStatus("Disconnected. Apply a profile or connect a source.");
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
    if (session_ == nullptr || !session_->connect(core_error)) {
      fail("Connect", qString(core_error));
      return;
    }
    connected_ = true;
    state_ = OperationState::Ready;
    emitLog("INFO", "Device", qString(adapters_.display_name) + " adapters connected");
    publishStatus(qString(adapters_.display_name) + " connected and ready");
    emit commandCompleted("Connect", qString(adapters_.display_name) + " is ready");
  }

  void disconnectRuntime() {
    if (running_) {
      stopRuntime(false, "Disconnected by operator");
    }
    if (session_ != nullptr) {
      session_->disconnect();
    }
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
      if (session_ == nullptr || !session_->connect(core_error)) {
        fail("Start", qString(core_error));
        return;
      }
      connected_ = true;
    }

    storage_.reset();
    udp_.reset();
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
      options.raw_stream.sample_format = SampleFormat::SignedInt16;
      if (active_source_ == AcquisitionSource::Alazar) {
        options.raw_stream.sample_format = SampleFormat::UnsignedOffsetBinary12LeftAligned;
      } else if (active_source_ == AcquisitionSource::Replay) {
        RawReplayReader descriptor_reader;
        std::string descriptor_error;
        if (!descriptor_reader.open(std::filesystem::path(config_.runtime.replay_file),
                                    descriptor_error)) {
          storage_.reset();
          fail("Start", qString(descriptor_error));
          return;
        }
        options.raw_stream.sample_format = descriptor_reader.streamDescriptor().sample_format;
      }
      options.raw_stream.sample_rate_hz = config_.digitizer.sample_rate_hz;
      options.raw_stream.record_length = config_.digitizer.sample_point;
      options.raw_stream.records_per_buffer = config_.digitizer.records_per_buffer;
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

    if (config_.udp.enabled) {
      udp_ = std::make_unique<UdpSenderService>();
      std::string udp_error;
      if (!udp_->start(config_.udp, config_.scan.x_pixel_count, config_.scan.y_line_count, udp_error)) {
        udp_.reset();
        stopStorage("UDP sender failed to start");
        fail("Start", qString(udp_error));
        return;
      }
    }

    processing_->setProcessedFrameCallback([this](ProcessedFramePtr frame) {
      if (udp_ != nullptr) {
        udp_->enqueue(frame);
      }
      if (storage_ != nullptr && config_.storage.processed_enabled) {
        std::string error_message;
        const auto result = storage_->enqueueProcessed(std::move(frame), error_message);
        if (result != EnqueueResult::Accepted) {
          storage_failure_pending_ = true;
        }
      }
    });

    std::string core_error;
    if (!processing_->start(core_error)) {
      stopStorage("Processing failed to start");
      stopUdp();
      fail("Start", qString(core_error));
      return;
    }

    state_ = OperationState::Preview;
    publishStatus("Starting devices in EDFA, digitizer, MCU order...");
    if (session_ == nullptr || !session_->start(core_error)) {
      const auto start_error = core_error;
      processing_->requestStop("Device start failed");
      processing_->waitUntilStopped(core_error);
      stopStorage("Device start failed");
      stopUdp();
      fail("Start", qString(start_error));
      return;
    }

    running_ = true;
    recording_ = config_.storage.raw_enabled || config_.storage.processed_enabled;
    storage_failure_pending_ = false;
    acquisition_accepting_.store(true);
    acquisition_worker_ = std::make_unique<ContinuousAcquisitionWorker>(*session_);
    if (!acquisition_worker_->start(
            [this](RawFrameBatchPtr batch, std::string& batch_error) {
              return consumeBatch(std::move(batch), batch_error);
            },
            [this](bool failed, std::string reason) {
              QMetaObject::invokeMethod(this,
                  [this, failed, reason = std::move(reason)] {
                    if (running_) {
                      stopRuntime(false, qString(reason), failed);
                    }
                  },
                  Qt::QueuedConnection);
            },
            core_error)) {
      const auto worker_error = core_error;
      acquisition_accepting_.store(false);
      running_ = false;
      std::string stop_error;
      session_->stop(stop_error);
      stopProcessing("Acquisition worker failed to start");
      stopUdp();
      stopStorage("Acquisition worker failed to start");
      acquisition_worker_.reset();
      fail("Start", qString(worker_error));
      return;
    }
    state_ = recording_ ? OperationState::Recording : OperationState::Acquiring;
    ui_timer_->setInterval(std::max(
        16, static_cast<int>(std::lround(1000.0 / std::clamp(config_.ui.plot_update_hz, 1.0, 60.0)))));
    ui_timer_->start();
    status_publish_timer_.restart();
    emitLog("INFO", "Acquisition",
            "Global START completed; continuous full-period DMA batches are active");
    if (udp_ != nullptr) {
      emitLog("INFO", "UDP", QString("Sending point frames to %1:%2")
                                 .arg(qString(config_.udp.target_ip))
                                 .arg(config_.udp.target_port));
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
    if (ui_timer_ != nullptr) {
      ui_timer_->stop();
    }
    acquisition_accepting_.store(false);
    if (acquisition_worker_ != nullptr) {
      acquisition_worker_->requestStop();
    }
    std::string error;
    if (session_ != nullptr) {
      session_->emergencyStop(error);
    }
    waitForAcquisitionWorker();
    stopProcessing("Emergency stop");
    stopUdp();
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

  void setSelectedAScanRuntime(std::uint32_t record_index) {
    selected_record_index_ = config_.digitizer.records_per_buffer == 0U
        ? 0U
        : std::min(record_index, config_.digitizer.records_per_buffer - 1U);
    if (processing_ != nullptr) {
      processing_->setSelectedRecordIndex(selected_record_index_);
    }
    emitLog("INFO", "Live View", QString("Selected A-scan %1 for Time Domain and FFT display")
                                     .arg(selected_record_index_));
  }

  void setLivePlotIndexRuntime(int plot_index) {
    const int next_index = std::clamp(plot_index, -1, 5);
    if (next_index == active_live_plot_index_) {
      return;
    }
    active_live_plot_index_ = next_index;
    switch (active_live_plot_index_) {
      case 0: last_waveform_snapshot_.reset(); break;
      case 1: last_fft_snapshot_.reset(); break;
      case 2:
      case 3: last_scan_line_snapshot_.reset(); break;
      case 4: last_bscan_snapshot_.reset(); break;
      case 5: last_point_cloud_snapshot_.reset(); break;
      default: break;
    }
    publishSnapshots();
  }

  void setEdfaOutputRuntime(bool enabled) {
    std::string error;
    if (adapters_.edfa == nullptr || !adapters_.edfa->setOutputEnabled(enabled, error)) {
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
    const auto x_count = derivedAScanCount(config_);
    const auto y_count = config_.scan.y_line_count;
    const auto point_count = derivedFramePointCount(config_);
    std::string error;
    auto frames = McuProtocol::buildFullFrameWaveform(config_, error);
    if (frames.empty()) {
      reject("MCU waveform", qString(error));
      return;
    }
    if (adapters_.mcu == nullptr || !adapters_.mcu->uploadWaveform(frames, error)) {
      reject("MCU waveform", qString(error));
      return;
    }
    emitLog("INFO", "MCU", QString("Uploaded one full frame: %1 A-scans x %2 B-scans = %3 points")
                                  .arg(x_count)
                                  .arg(y_count)
                                  .arg(point_count));
    publishStatus("MCU waveform loaded");
    emit commandCompleted("MCU waveform", QString("%1-point full-frame waveform uploaded").arg(point_count));
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
    if (session_ != nullptr) {
      session_->disconnect();
    }
    connected_ = false;
  }

 signals:
  void statusChanged(fmcw::RuntimeStatus status);
  void waveformReady(fmcw::WaveformSnapshotPtr snapshot);
  void fftReady(fmcw::FftSnapshotPtr snapshot);
  void scanLineReady(fmcw::ScanLineSnapshotPtr snapshot);
  void bscanReady(fmcw::BScanSnapshotPtr snapshot);
  void pointCloudReady(fmcw::PointCloudSnapshotPtr snapshot);
  void segmentationSnapshotReady(fmcw::WaveformSnapshotPtr snapshot);
  void logMessage(QString level, QString source, QString message);
  void commandFailed(QString command, QString message);
  void commandCompleted(QString command, QString message);

 private:
  bool selectRuntimeAdapters(AcquisitionSource source, QString& error) {
    if (session_ != nullptr && active_source_ == source) {
      return true;
    }
    if (acquisition_worker_ != nullptr) {
      error = "Stop the acquisition worker before changing runtime source";
      return false;
    }
    session_.reset();
    adapters_ = createRuntimeAdapters(source);
    if (!adapters_) {
      error = "Runtime adapter factory could not create the selected source";
      return false;
    }
    session_ = std::make_unique<AcquisitionSession>(
        *adapters_.digitizer, *adapters_.edfa, *adapters_.mcu);
    active_source_ = source;
    error.clear();
    return true;
  }

  bool configureRuntime(const SystemConfig& config, QString& error) {
    if (running_) {
      error = "Stop acquisition before applying hardware or FFT backend changes";
      return false;
    }

    const bool reconnect = connected_;
    if (reconnect) {
      if (session_ != nullptr) {
        session_->disconnect();
      }
      connected_ = false;
    }
    if (!selectRuntimeAdapters(config.runtime.acquisition_source, error)) {
      return false;
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
    selected_record_index_ = std::min(selected_record_index_, config.digitizer.records_per_buffer - 1U);
    processing_->setSelectedRecordIndex(selected_record_index_);
    if (session_ == nullptr || !session_->configure(config, next_revision, core_error)) {
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
      if (!session_->connect(core_error)) {
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

  bool consumeBatch(RawFrameBatchPtr batch, std::string& error) {
    if (!acquisition_accepting_.load() || processing_ == nullptr || !batch) {
      error = "Runtime is no longer accepting DMA batches";
      return false;
    }
    if (storage_ != nullptr && config_.storage.raw_enabled) {
      const auto storage_result = storage_->enqueueRawBatch(batch, error);
      if (storage_result != EnqueueResult::Accepted) {
        if (error.empty()) {
          error = "Raw storage queue stopped";
        }
        return false;
      }
    }
    const auto processing_result = processing_->enqueueBatch(std::move(batch), error);
    if (processing_result != ProcessingEnqueueResult::Accepted) {
      if (error.empty()) {
        error = "Processing batch queue stopped";
      }
      return false;
    }
    if (storage_failure_pending_.load()) {
      error = "Processed storage queue stopped";
      return false;
    }
    error.clear();
    return true;
  }

  void publishPeriodic() {
    if (!running_ || processing_ == nullptr) {
      return;
    }
    if (storage_failure_pending_.load()) {
      stopRuntime(false, "Processed storage queue stopped", true);
      return;
    }
    std::string processing_stop_reason;
    if (processing_->stopRequested(processing_stop_reason)) {
      stopRuntime(false, qString(processing_stop_reason), true);
      return;
    }
    if (storage_ != nullptr && storage_->status().stop_requested) {
      stopRuntime(false, qString(storage_->status().stop_reason), true);
      return;
    }
    publishSnapshots();
    if (!status_publish_timer_.isValid() ||
        status_publish_timer_.elapsed() >= kStatusUpdateIntervalMs) {
      status_publish_timer_.restart();
      publishStatus(recording_ ? "Acquiring DMA batches and recording" : "Acquiring DMA batches");
    }
  }

  void publishSnapshots() {
    if (processing_ == nullptr) {
      return;
    }
    switch (active_live_plot_index_) {
      case 0: {
        const auto snapshot = processing_->snapshots().latestWaveform();
        if (snapshot != nullptr && snapshot != last_waveform_snapshot_) {
          last_waveform_snapshot_ = snapshot;
          emit waveformReady(snapshot);
        }
        break;
      }
      case 1: {
        const auto snapshot = processing_->snapshots().latestFft();
        if (snapshot != nullptr && snapshot != last_fft_snapshot_) {
          last_fft_snapshot_ = snapshot;
          emit fftReady(snapshot);
        }
        break;
      }
      case 2:
      case 3: {
        const auto snapshot = processing_->snapshots().latestScanLine();
        if (snapshot != nullptr && snapshot != last_scan_line_snapshot_) {
          last_scan_line_snapshot_ = snapshot;
          emit scanLineReady(snapshot);
        }
        break;
      }
      case 4: {
        const auto snapshot = processing_->snapshots().latestBScan();
        if (snapshot != nullptr && snapshot != last_bscan_snapshot_) {
          last_bscan_snapshot_ = snapshot;
          emit bscanReady(snapshot);
        }
        break;
      }
      case 5: {
        const auto snapshot = processing_->snapshots().latestPointCloud();
        if (snapshot != nullptr && snapshot != last_point_cloud_snapshot_) {
          last_point_cloud_snapshot_ = snapshot;
          emit pointCloudReady(snapshot);
        }
        break;
      }
      default:
        break;
    }
  }

  void stopRuntime(bool emergency, const QString& reason, bool error_state = false) {
    if (ui_timer_ != nullptr) {
      ui_timer_->stop();
    }
    acquisition_accepting_.store(false);
    if (acquisition_worker_ != nullptr) {
      acquisition_worker_->requestStop();
    }
    state_ = OperationState::Stopping;
    publishStatus("Stopping MCU trigger, digitizer, processing, storage, and EDFA...");
    std::string error;
    if (session_ != nullptr) {
      if (emergency) {
        session_->emergencyStop(error);
      } else {
        session_->stop(error);
      }
    }
    waitForAcquisitionWorker();
    stopProcessing(reason);
    stopUdp();
    stopStorage(reason);
    running_ = false;
    recording_ = false;
    state_ = error_state || !error.empty()
        ? OperationState::Error
        : connected_ ? OperationState::Ready : OperationState::Configured;
    emitLog(error.empty() ? "INFO" : "ERROR", "Acquisition",
            error.empty() ? QString("Stopped: %1").arg(reason) : qString(error));
    publishSnapshots();
    publishStatus(error.empty() ? reason : qString(error));
  }

  void waitForAcquisitionWorker() {
    if (acquisition_worker_ == nullptr) {
      return;
    }
    std::string worker_error;
    acquisition_worker_->waitUntilStopped(worker_error);
    acquisition_status_ = acquisition_worker_->status();
    if (!worker_error.empty()) {
      emitLog("ERROR", "Acquisition worker", qString(worker_error));
    }
    acquisition_worker_.reset();
  }

  void stopProcessing(const QString& reason) {
    if (processing_ == nullptr) {
      return;
    }
    processing_->requestStop(reason.toStdString());
    std::string error;
    processing_->waitUntilStopped(error);
  }

  void stopUdp() {
    if (udp_ == nullptr) {
      return;
    }
    udp_->stop();
    const auto status = udp_->status();
    emitLog(status.send_errors == 0U ? "INFO" : "ERROR", "UDP",
            QString("UDP stopped: %1 frames, %2 packets, %3 dropped")
                .arg(status.frames_sent)
                .arg(status.packets_sent)
                .arg(status.dropped_frames));
    udp_.reset();
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
    status.source_name = qString(toString(active_source_));
    AcquisitionTelemetrySnapshot telemetry;
    if (session_ != nullptr) {
      telemetry = session_->telemetry();
    }
    const auto acquisition_status = acquisition_worker_ != nullptr
        ? acquisition_worker_->status()
        : acquisition_status_;
    status.acquisition_batches_delivered = acquisition_status.batches_delivered;
    status.digitizer_ready = telemetry.digitizer.device.ready;
    status.edfa_ready = telemetry.edfa.device.ready;
    status.edfa_bypassed = telemetry.edfa.bypassed;
    status.edfa_output_enabled = telemetry.edfa.output_enabled;
    status.mcu_ready = telemetry.mcu.device.ready;
    status.mcu_bypassed = !config_.mcu.enabled;
    status.mcu_waveform_loaded = telemetry.mcu.waveform_points > 0U;
    status.mcu_waveform_points = telemetry.mcu.waveform_points;
    status.mcu_frame_time_ms = telemetry.mcu.waveform_points == 0U || !(config_.scan.scanner_sample_rate_hz > 0.0)
        ? 0.0
        : static_cast<double>(telemetry.mcu.waveform_points) * 1000.0 / config_.scan.scanner_sample_rate_hz;
    status.frames_received = telemetry.digitizer.frames_received;
    status.dma_buffers_received = telemetry.digitizer.dma_buffers_received;
    status.dma_buffer_drops = telemetry.digitizer.dma_buffer_drops;
    status.trigger_misses = telemetry.digitizer.trigger_misses;
    status.dma_bscan_rate_hz = telemetry.digitizer.dma_buffer_rate_hz;
    status.dma_bscan_period_ms = telemetry.digitizer.dma_buffer_period_ms;
    if (processing_ != nullptr) {
      const auto processing_status = processing_->status();
      status.frames_processed = processing_status.frames_processed;
      status.processing_queue_size = processing_status.queue_size;
      status.processing_queue_capacity = processing_status.queue_capacity;
      status.processing_latency_ms = processing_status.average_latency_ms;
      status.processing_batch_latency_ms = processing_status.last_batch_latency_ms;
      status.processing_batch_average_ms = processing_status.average_batch_latency_ms;
      status.processing_copy_latency_ms = processing_status.last_ownership_copy_latency_ms;
      status.processing_signal_latency_ms = processing_status.last_signal_processing_latency_ms;
      status.processing_batch_p50_ms = processing_status.batch_latency_p50_ms;
      status.processing_batch_p95_ms = processing_status.batch_latency_p95_ms;
      status.processing_batch_p99_ms = processing_status.batch_latency_p99_ms;
      status.processing_batch_max_ms = processing_status.maximum_batch_latency_ms;
      status.processing_deadline_ms = processing_status.batch_deadline_ms;
      status.processing_deadline_misses = processing_status.batch_deadline_misses;
      status.backend_name = qString(processing_status.backend_name);
      status.processing_revision = processing_status.processing_config_revision;
    }
    if (storage_ != nullptr) {
      const auto storage_status = storage_->status();
      status.frames_written = storage_status.raw_writer.frames_written +
                              storage_status.processed_writer.frames_written;
      status.storage_queue_size = storage_status.queue_size;
      status.storage_queue_capacity = storage_status.queue_capacity;
      status.raw_storage_queue_size = storage_status.raw_queue_size;
      status.raw_storage_queue_capacity = storage_status.raw_queue_capacity;
      status.raw_storage_queue_high_water = storage_status.raw_queue_high_water_mark;
      status.processed_storage_queue_size = storage_status.processed_queue_size;
      status.processed_storage_queue_capacity = storage_status.processed_queue_capacity;
      status.processed_storage_queue_high_water = storage_status.processed_queue_high_water_mark;
      status.raw_blocks_written = storage_status.raw_writer.blocks_written;
      status.raw_bytes_written = storage_status.raw_writer.bytes_written;
      status.storage_failed = storage_status.failed;
      status.storage_throughput_mbps = storage_status.raw_writer.throughput_mbps +
                                       storage_status.processed_writer.throughput_mbps;
      status.raw_storage_throughput_mbps = storage_status.raw_writer.throughput_mbps;
      status.processed_storage_throughput_mbps = storage_status.processed_writer.throughput_mbps;
      status.storage_stop_reason = storage_status.failed ? qString(storage_status.stop_reason) : QString{};
    }
    if (udp_ != nullptr) {
      const auto udp_status = udp_->status();
      status.udp_running = udp_status.running;
      status.udp_frames_sent = udp_status.frames_sent;
      status.udp_packets_sent = udp_status.packets_sent;
      status.udp_dropped_frames = udp_status.dropped_frames;
      status.udp_queue_size = udp_status.queue_size;
      status.udp_queue_capacity = udp_status.queue_capacity;
      status.udp_send_fps = udp_status.send_fps;
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
  QTimer* ui_timer_ = nullptr;
  QElapsedTimer status_publish_timer_;
  RuntimeAdapters adapters_;
  std::unique_ptr<AcquisitionSession> session_;
  std::unique_ptr<ContinuousAcquisitionWorker> acquisition_worker_;
  std::unique_ptr<ProcessingService> processing_;
  std::unique_ptr<AsyncStorageService> storage_;
  std::unique_ptr<UdpSenderService> udp_;
  SystemConfig config_;
  OperationState state_ = OperationState::Disconnected;
  std::uint64_t config_revision_ = 0;
  std::uint64_t processing_revision_ = 0;
  std::uint32_t selected_record_index_ = 0;
  int active_live_plot_index_ = -1;
  WaveformSnapshotPtr last_waveform_snapshot_;
  FftSnapshotPtr last_fft_snapshot_;
  ScanLineSnapshotPtr last_scan_line_snapshot_;
  BScanSnapshotPtr last_bscan_snapshot_;
  PointCloudSnapshotPtr last_point_cloud_snapshot_;
  AcquisitionSource active_source_ = AcquisitionSource::Simulator;
  ContinuousAcquisitionStatus acquisition_status_;
  bool configured_ = false;
  bool connected_ = false;
  bool running_ = false;
  bool recording_ = false;
  std::atomic_bool acquisition_accepting_{false};
  std::atomic_bool storage_failure_pending_{false};
};

ApplicationController::ApplicationController(QString platform_name, QObject* parent) : QObject(parent) {
  qRegisterMetaType<RuntimeStatus>();
  qRegisterMetaType<WaveformSnapshotPtr>();
  qRegisterMetaType<FftSnapshotPtr>();
  qRegisterMetaType<ScanLineSnapshotPtr>();
  qRegisterMetaType<BScanSnapshotPtr>();
  qRegisterMetaType<PointCloudSnapshotPtr>();

  worker_ = new RuntimeWorker(std::move(platform_name));
  worker_->moveToThread(&runtime_thread_);
  connect(&runtime_thread_, &QThread::started, worker_, &RuntimeWorker::initialize);
  connect(&runtime_thread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(worker_, &RuntimeWorker::statusChanged, this, &ApplicationController::statusChanged);
  connect(worker_, &RuntimeWorker::waveformReady, this, &ApplicationController::waveformReady);
  connect(worker_, &RuntimeWorker::fftReady, this, &ApplicationController::fftReady);
  connect(worker_, &RuntimeWorker::scanLineReady, this, &ApplicationController::scanLineReady);
  connect(worker_, &RuntimeWorker::bscanReady, this, &ApplicationController::bscanReady);
  connect(worker_, &RuntimeWorker::pointCloudReady, this, &ApplicationController::pointCloudReady);
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

void ApplicationController::setSelectedAScan(std::uint32_t record_index) {
  QMetaObject::invokeMethod(worker_, [worker = worker_, record_index] {
    worker->setSelectedAScanRuntime(record_index);
  }, Qt::QueuedConnection);
}

void ApplicationController::setLivePlotIndex(int plot_index) {
  QMetaObject::invokeMethod(worker_, [worker = worker_, plot_index] {
    worker->setLivePlotIndexRuntime(plot_index);
  }, Qt::QueuedConnection);
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
