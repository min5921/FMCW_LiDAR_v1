#pragma once

#include "core/config_types.h"
#include "core/system_state.h"
#include "processing/processing_snapshots.h"

#include <QObject>
#include <QString>
#include <QThread>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace fmcw {

using WaveformSnapshotPtr = std::shared_ptr<const WaveformSnapshot>;
using FftSnapshotPtr = std::shared_ptr<const FftSnapshot>;
using ScanLineSnapshotPtr = std::shared_ptr<const ScanLineSnapshot>;
using BScanSnapshotPtr = std::shared_ptr<const BScanSnapshot>;

struct RuntimeStatus {
  OperationState state = OperationState::Disconnected;
  bool configured = false;
  bool connected = false;
  bool running = false;
  bool recording = false;
  bool digitizer_ready = false;
  bool edfa_ready = false;
  bool edfa_bypassed = true;
  bool edfa_output_enabled = false;
  bool mcu_ready = false;
  bool mcu_bypassed = true;
  bool mcu_waveform_loaded = false;
  std::uint64_t config_revision = 0;
  std::uint64_t processing_revision = 0;
  std::uint64_t frames_received = 0;
  std::uint64_t frames_processed = 0;
  std::uint64_t frames_written = 0;
  std::size_t processing_queue_size = 0;
  std::size_t processing_queue_capacity = 0;
  std::size_t storage_queue_size = 0;
  std::size_t storage_queue_capacity = 0;
  double processing_latency_ms = 0.0;
  double storage_throughput_mbps = 0.0;
  QString backend_name;
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
  void startSystem(const SystemConfig& config);
  void stopSystem();
  void emergencyStop();
  void updateProcessing(const ProcessingConfig& config);
  void setEdfaOutput(bool enabled);
  void uploadMcuWaveform();
  void captureSegmentationSnapshot();

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
  QThread runtime_thread_;
  RuntimeWorker* worker_ = nullptr;
};

}  // namespace fmcw

Q_DECLARE_METATYPE(fmcw::RuntimeStatus)
Q_DECLARE_METATYPE(fmcw::WaveformSnapshotPtr)
Q_DECLARE_METATYPE(fmcw::FftSnapshotPtr)
Q_DECLARE_METATYPE(fmcw::ScanLineSnapshotPtr)
Q_DECLARE_METATYPE(fmcw::BScanSnapshotPtr)
