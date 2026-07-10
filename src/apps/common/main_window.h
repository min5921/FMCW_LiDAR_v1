#pragma once

#include "apps/common/application_controller.h"
#include "apps/common/plot_widgets.h"
#include "core/config_types.h"

#include <QMainWindow>
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
  bool validateControls(bool show_dialog = false);
  SystemConfig configFromControls() const;
  void loadConfigToControls(const SystemConfig& config);
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
  bool freeze_live_ = false;
  QStringList log_entries_;

  QListWidget* navigation_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QComboBox* profile_combo_ = nullptr;
  QLabel* validation_label_ = nullptr;
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
  LinePlotWidget* time_plot_ = nullptr;
  LinePlotWidget* fft_plot_ = nullptr;
  LinePlotWidget* peak_index_plot_ = nullptr;
  LinePlotWidget* peak_value_plot_ = nullptr;
  LinePlotWidget* distance_plot_ = nullptr;
  HeatmapWidget* bscan_plot_ = nullptr;
  SegmentationPlotWidget* segmentation_plot_ = nullptr;
  QLabel* segmentation_state_ = nullptr;

  QComboBox* digitizer_channel_ = nullptr;
  QDoubleSpinBox* sample_rate_ = nullptr;
  QSpinBox* sample_point_ = nullptr;
  QSpinBox* records_per_buffer_ = nullptr;
  QSpinBox* dma_buffer_count_ = nullptr;
  QDoubleSpinBox* input_range_ = nullptr;
  QComboBox* coupling_ = nullptr;
  QDoubleSpinBox* trigger_level_ = nullptr;

  QDoubleSpinBox* wavelength_ = nullptr;
  QDoubleSpinBox* sweep_bandwidth_ = nullptr;
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
  QSpinBox* x_pixels_ = nullptr;
  QSpinBox* y_lines_ = nullptr;
  QDoubleSpinBox* line_time_ = nullptr;
  QCheckBox* bidirectional_ = nullptr;
  QCheckBox* mcu_enabled_ = nullptr;
  QLineEdit* mcu_port_ = nullptr;
  QPushButton* upload_waveform_button_ = nullptr;
  QLabel* mcu_waveform_state_ = nullptr;

  QComboBox* fft_backend_ = nullptr;
  QComboBox* window_function_ = nullptr;
  QCheckBox* dc_removal_ = nullptr;
  QCheckBox* normalize_ = nullptr;
  QDoubleSpinBox* peak_threshold_ = nullptr;
  QSpinBox* peak_start_ = nullptr;
  QSpinBox* peak_end_ = nullptr;
  QCheckBox* peak_tracking_ = nullptr;
  QSpinBox* peak_delta_ = nullptr;
  QSpinBox* reacquire_width_ = nullptr;
  QComboBox* lost_policy_ = nullptr;
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

  QComboBox* log_filter_ = nullptr;
  QPlainTextEdit* log_view_ = nullptr;
};

}  // namespace fmcw
