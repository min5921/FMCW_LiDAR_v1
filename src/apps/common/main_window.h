#pragma once

#include "apps/common/application_controller.h"
#include "apps/common/plot_widgets.h"
#include "apps/common/point_cloud_widget.h"
#include "core/config_types.h"

#include <QMainWindow>
#include <QElapsedTimer>
#include <QList>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QWidget;

namespace fmcw {

class MainWindow final : public QMainWindow {
 public:
  explicit MainWindow(QString platform_name, QWidget* parent = nullptr);

  void startDemo();
  void captureSegmentation();
  void showPage(int index);
  void showLiveTab(int index);
  void setDarkTheme(bool enabled = true);
  bool savePointCloudFramebuffer(const QString& path);

 private:
  QWidget* buildOverviewPage();
  QWidget* buildLivePage();
  QWidget* buildDigitizerPage();
  QWidget* buildLaserEdfaPage();
  QWidget* buildScanMcuPage();
  QWidget* buildProcessingPage();
  QWidget* buildStorageUdpPage();
  QWidget* buildLogPage();
  QWidget* makePage(QString title, QWidget* content);

  void connectUi();
  void markDirty();
  void markRestartDirty();
  bool validateControls(bool show_dialog = false);
  void showValidationDetails();
  SystemConfig configFromControls() const;
  void loadConfigToControls(const SystemConfig& config, bool mark_pending = false);
  void populateDigitizerCapabilities(QString profile_id, double preferred_rate_hz,
                                     double preferred_range_volts, std::uint32_t preferred_impedance);
  void updateDerivedAcquisitionLabels();
  void applyProfile();
  void loadProfile();
  void saveProfile();
  void updateStatus(RuntimeStatus status);
  void appendLog(QString level, QString source, QString message);
  void rebuildLog();
  void setStatusText(QLabel* label, QString text, bool ready, bool bypassed = false);
  void saveCurrentView();

  QString platform_name_;
  ApplicationController* controller_ = nullptr;
  SystemConfig config_;
  RuntimeStatus runtime_status_;
  bool config_dirty_ = false;
  bool restart_dirty_ = false;
  bool loading_controls_ = false;
  bool freeze_live_ = false;
  QStringList log_entries_;

  QListWidget* navigation_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QComboBox* profile_combo_ = nullptr;
  QPushButton* validation_button_ = nullptr;
  QLabel* runtime_state_label_ = nullptr;
  QLabel* raw_indicator_ = nullptr;
  QLabel* udp_indicator_ = nullptr;
  QPushButton* connect_button_ = nullptr;
  QPushButton* apply_button_ = nullptr;
  QPushButton* start_stop_button_ = nullptr;
  QPushButton* emergency_button_ = nullptr;
  QToolButton* load_button_ = nullptr;
  QToolButton* save_button_ = nullptr;

  QLabel* overview_digitizer_ = nullptr;
  QLabel* overview_edfa_ = nullptr;
  QLabel* overview_mcu_ = nullptr;
  QLabel* overview_processing_ = nullptr;
  QLabel* overview_frames_ = nullptr;
  QLabel* overview_queues_ = nullptr;
  QLabel* overview_latency_ = nullptr;
  QLabel* overview_recording_ = nullptr;
  QLabel* overview_detail_ = nullptr;

  QTabWidget* live_tabs_ = nullptr;
  QToolButton* freeze_button_ = nullptr;
  QSpinBox* selected_a_scan_ = nullptr;
  QLabel* selected_a_scan_status_ = nullptr;
  LinePlotWidget* time_plot_ = nullptr;
  LinePlotWidget* fft_plot_ = nullptr;
  LinePlotWidget* peak_index_plot_ = nullptr;
  LinePlotWidget* peak_value_plot_ = nullptr;
  LinePlotWidget* distance_plot_ = nullptr;
  HeatmapWidget* bscan_plot_ = nullptr;
  PointCloudWidget* point_cloud_plot_ = nullptr;
  QLabel* point_cloud_status_ = nullptr;
  QElapsedTimer point_cloud_update_timer_;
  SegmentationPlotWidget* segmentation_plot_ = nullptr;
  QLabel* segmentation_state_ = nullptr;

  QComboBox* digitizer_channel_ = nullptr;
  QComboBox* board_profile_ = nullptr;
  QLabel* board_address_ = nullptr;
  QComboBox* sample_rate_ = nullptr;
  QSpinBox* sample_point_ = nullptr;
  QSpinBox* records_per_buffer_ = nullptr;
  QSpinBox* dma_buffer_count_ = nullptr;
  QComboBox* input_range_ = nullptr;
  QComboBox* impedance_ = nullptr;
  QComboBox* coupling_ = nullptr;
  QComboBox* trigger_slope_ = nullptr;
  QDoubleSpinBox* trigger_level_ = nullptr;
  QSpinBox* trigger_delay_ = nullptr;
  QSpinBox* pre_trigger_ = nullptr;
  QLabel* trigger_level_code_ = nullptr;
  QLabel* post_trigger_ = nullptr;
  QLabel* digitizer_lock_state_ = nullptr;

  QDoubleSpinBox* wavelength_ = nullptr;
  QDoubleSpinBox* sweep_bandwidth_ = nullptr;
  QDoubleSpinBox* sweep_rate_ = nullptr;
  QDoubleSpinBox* chirp_period_ = nullptr;
  QDoubleSpinBox* laser_power_ = nullptr;
  QComboBox* edfa_mode_ = nullptr;
  QLineEdit* edfa_port_ = nullptr;
  QComboBox* edfa_control_mode_ = nullptr;
  QDoubleSpinBox* edfa_setpoint_ = nullptr;
  QSpinBox* edfa_warmup_ = nullptr;
  QPushButton* edfa_output_button_ = nullptr;

  QDoubleSpinBox* x_start_ = nullptr;
  QDoubleSpinBox* x_end_ = nullptr;
  QDoubleSpinBox* y_start_ = nullptr;
  QDoubleSpinBox* y_end_ = nullptr;
  QLabel* a_scan_count_ = nullptr;
  QSpinBox* y_lines_ = nullptr;
  QLabel* frame_point_count_ = nullptr;
  QLabel* dma_bscan_rate_ = nullptr;
  QLabel* frame_time_ = nullptr;
  QLabel* mcu_point_rate_ = nullptr;
  QLabel* mcu_frame_time_ = nullptr;
  QLabel* frame_sync_state_ = nullptr;
  QCheckBox* bidirectional_ = nullptr;
  QCheckBox* mcu_enabled_ = nullptr;
  QLineEdit* mcu_port_ = nullptr;
  QPushButton* upload_waveform_button_ = nullptr;
  QLabel* mcu_waveform_state_ = nullptr;

  QComboBox* fft_backend_ = nullptr;
  QComboBox* window_function_ = nullptr;
  QCheckBox* dc_removal_ = nullptr;
  QDoubleSpinBox* peak_threshold_ = nullptr;
  QSpinBox* peak_start_ = nullptr;
  QSpinBox* peak_end_ = nullptr;
  QSpinBox* up_start_ = nullptr;
  QSpinBox* up_end_ = nullptr;
  QSpinBox* down_start_ = nullptr;
  QSpinBox* down_end_ = nullptr;
  QSpinBox* guard_samples_ = nullptr;
  QSpinBox* fft_length_ = nullptr;

  QCheckBox* raw_enabled_ = nullptr;
  QCheckBox* processed_enabled_ = nullptr;
  QLineEdit* output_directory_ = nullptr;
  QSpinBox* storage_queue_ = nullptr;
  QDoubleSpinBox* split_size_ = nullptr;
  QCheckBox* udp_enabled_ = nullptr;
  QLineEdit* udp_ip_ = nullptr;
  QSpinBox* udp_port_ = nullptr;
  QSpinBox* udp_points_ = nullptr;
  QComboBox* udp_version_ = nullptr;
  QSpinBox* udp_queue_ = nullptr;
  QComboBox* udp_policy_ = nullptr;
  QLabel* udp_status_ = nullptr;

  QComboBox* log_filter_ = nullptr;
  QPlainTextEdit* log_view_ = nullptr;
  QList<QWidget*> restart_required_controls_;
};

}  // namespace fmcw
