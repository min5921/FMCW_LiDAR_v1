#pragma once

#include "core/config_types.h"
#include "core/device_interfaces.h"
#include "core/system_state.h"
#include "processing/processing_snapshots.h"

#include <QObject>
#include <QString>
#include <QThread>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace fmcw {

using WaveformSnapshotPtr = std::shared_ptr<const WaveformSnapshot>;
using FftSnapshotPtr = std::shared_ptr<const FftSnapshot>;
using ScanLineSnapshotPtr = std::shared_ptr<const ScanLineSnapshot>;
using BScanSnapshotPtr = std::shared_ptr<const BScanSnapshot>;
using PointCloudSnapshotPtr = std::shared_ptr<const PointCloudSnapshot>;

struct UiDispatchMetrics {
  std::uint64_t waveform_published = 0;
  std::uint64_t waveform_coalesced = 0;
  std::uint64_t fft_published = 0;
  std::uint64_t fft_coalesced = 0;
};

struct RuntimeStatus {
  OperationState state = OperationState::Disconnected;
  bool configured = false;
  bool connected = false;
  bool running = false;
  bool recording = false;
  bool digitizer_ready = false;
  bool edfa_ready = false;
  bool edfa_bypassed = true;
  bool edfa_connected = false;
  bool edfa_output_enabled = false;
  bool edfa_telemetry_valid = false;
  double edfa_target_dbm = 0.0;
  double edfa_current_ma = 0.0;
  double edfa_input_dbm = 0.0;
  double edfa_output_dbm = 0.0;
  bool mcu_ready = false;
  bool mcu_bypassed = true;
  bool mcu_scan_active = false;
  bool mcu_waveform_loaded = false;
  std::uint32_t mcu_waveform_points = 0;
  double mcu_frame_time_ms = 0.0;
  std::uint64_t acquisition_batches_delivered = 0;
  std::uint64_t dma_buffers_received = 0;
  std::uint64_t dma_buffer_drops = 0;
  std::uint64_t trigger_misses = 0;
  double dma_bscan_rate_hz = 0.0;
  double dma_bscan_period_ms = 0.0;
  std::uint32_t dma_buffers_configured = 0;
  std::uint32_t dma_buffers_posted = 0;
  std::uint32_t dma_buffers_in_use = 0;
  double oldest_dma_lease_ms = 0.0;
  std::uint64_t config_revision = 0;
  std::uint64_t processing_revision = 0;
  std::uint64_t frames_received = 0;
  std::uint64_t frames_processed = 0;
  std::uint64_t frames_written = 0;
  std::size_t processing_queue_size = 0;
  std::size_t processing_queue_capacity = 0;
  std::size_t storage_queue_size = 0;
  std::size_t storage_queue_capacity = 0;
  std::size_t raw_storage_queue_size = 0;
  std::size_t raw_storage_queue_capacity = 0;
  std::size_t raw_storage_queue_high_water = 0;
  std::size_t processed_storage_queue_size = 0;
  std::size_t processed_storage_queue_capacity = 0;
  std::size_t processed_storage_queue_high_water = 0;
  std::uint64_t raw_blocks_written = 0;
  std::uint64_t raw_bytes_written = 0;
  bool storage_failed = false;
  double processing_latency_ms = 0.0;
  double processing_batch_latency_ms = 0.0;
  double processing_batch_average_ms = 0.0;
  double processing_copy_latency_ms = 0.0;
  double processing_signal_latency_ms = 0.0;
  double processing_batch_p50_ms = 0.0;
  double processing_batch_p95_ms = 0.0;
  double processing_batch_p99_ms = 0.0;
  double processing_batch_max_ms = 0.0;
  double processing_deadline_ms = 5.0;
  std::uint64_t processing_deadline_misses = 0;
  double storage_throughput_mbps = 0.0;
  double raw_storage_throughput_mbps = 0.0;
  double processed_storage_throughput_mbps = 0.0;
  bool udp_running = false;
  std::uint64_t udp_frames_sent = 0;
  std::uint64_t udp_packets_sent = 0;
  std::uint64_t udp_dropped_frames = 0;
  std::size_t udp_queue_size = 0;
  std::size_t udp_queue_capacity = 0;
  double udp_send_fps = 0.0;
  QString source_name;
  QString edfa_port;
  QString edfa_device_name;
  QString edfa_control_mode;
  QString edfa_detail;
  QString backend_name;
  QString storage_stop_reason;
  QString active_operation;
  QString mcu_last_ack;
  QString mcu_detail;
  QString detail;
};

class RuntimeWorker;

class ApplicationController final : public QObject {
  Q_OBJECT

 public:
  explicit ApplicationController(QString platform_name, QObject* parent = nullptr);
  ~ApplicationController() override;

  ApplicationController(const ApplicationController&) = delete;
  ApplicationController& operator=(const ApplicationController&) = delete;

  void applyConfig(const SystemConfig& config);
  void connectSystem(const SystemConfig& config);
  void disconnectSystem();
  void startSystem();
  void stopSystem();
  void emergencyStop();
  void updateProcessing(const ProcessingConfig& config);
  void setSelectedAScan(std::uint32_t record_index);
  void setLivePlotIndex(int plot_index);
  void setEdfaOutput(bool enabled);
  void uploadMcuWaveform();
  void captureSegmentationSnapshot();
  UiDispatchMetrics takeUiDispatchMetrics();

 signals:
  void statusChanged(fmcw::RuntimeStatus status);
  void waveformReady(fmcw::WaveformSnapshotPtr snapshot);
  void fftReady(fmcw::FftSnapshotPtr snapshot);
  void scanLineReady(fmcw::ScanLineSnapshotPtr snapshot);
  void bscanReady(fmcw::BScanSnapshotPtr snapshot);
  void pointCloudReady(fmcw::PointCloudSnapshotPtr snapshot);
  void segmentationSnapshotReady(fmcw::WaveformSnapshotPtr snapshot);
  void mcuUploadProgressChanged(fmcw::McuUploadProgress progress);
  void logMessage(QString level, QString source, QString message);
  void commandFailed(QString command, QString message);
  void commandCompleted(QString command, QString message);

 private:
  void enqueueStatus(RuntimeStatus status);
  void enqueueWaveform(WaveformSnapshotPtr snapshot);
  void enqueueFft(FftSnapshotPtr snapshot);
  void enqueueScanLine(ScanLineSnapshotPtr snapshot);
  void enqueueBScan(BScanSnapshotPtr snapshot);
  void enqueuePointCloud(PointCloudSnapshotPtr snapshot);
  void schedulePendingUiDispatchLocked();
  void drainPendingUiUpdates();

  QThread runtime_thread_;
  RuntimeWorker* worker_ = nullptr;
  std::mutex pending_ui_mutex_;
  std::optional<RuntimeStatus> pending_status_;
  WaveformSnapshotPtr pending_waveform_;
  FftSnapshotPtr pending_fft_;
  ScanLineSnapshotPtr pending_scan_line_;
  BScanSnapshotPtr pending_bscan_;
  PointCloudSnapshotPtr pending_point_cloud_;
  bool ui_dispatch_scheduled_ = false;
  UiDispatchMetrics ui_dispatch_metrics_;
};

}  // namespace fmcw

Q_DECLARE_METATYPE(fmcw::RuntimeStatus)
Q_DECLARE_METATYPE(fmcw::McuUploadProgress)
Q_DECLARE_METATYPE(fmcw::WaveformSnapshotPtr)
Q_DECLARE_METATYPE(fmcw::FftSnapshotPtr)
Q_DECLARE_METATYPE(fmcw::ScanLineSnapshotPtr)
Q_DECLARE_METATYPE(fmcw::BScanSnapshotPtr)
Q_DECLARE_METATYPE(fmcw::PointCloudSnapshotPtr)
