#include "apps/common/main_window.h"

#include "core/app_version.h"
#include "core/config_profile.h"
#include "core/config_validation.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <utility>

namespace fmcw {
namespace {

QGroupBox* groupBox(const QString& title, QWidget* parent = nullptr) {
  auto* group = new QGroupBox(title, parent);
  group->setProperty("surface", true);
  return group;
}

QFrame* statusCard(const QString& title, QLabel*& value, QWidget* parent) {
  auto* frame = new QFrame(parent);
  frame->setProperty("card", true);
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(16, 13, 16, 13);
  layout->setSpacing(7);
  auto* caption = new QLabel(title, frame);
  caption->setProperty("caption", true);
  value = new QLabel("--", frame);
  value->setProperty("metric", true);
  value->setWordWrap(true);
  layout->addWidget(caption);
  layout->addWidget(value);
  return frame;
}

QVector<float> qVector(const std::vector<float>& values) {
  return QVector<float>(values.begin(), values.end());
}

void repolish(QWidget* widget) {
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  widget->update();
}

QWidget* wrapInScrollArea(QWidget* content) {
  auto* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(content);
  return scroll;
}

std::filesystem::path fileSystemPath(const QString& path) {
#ifdef Q_OS_WIN
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toStdString());
#endif
}

}  // namespace

MainWindow::MainWindow(QString platform_name, QWidget* parent)
    : QMainWindow(parent), platform_name_(std::move(platform_name)) {
  setWindowTitle(QString("FMCW LiDAR v%1 - %2").arg(QString::fromStdString(versionString()), platform_name_));
  setMinimumSize(1180, 720);
  resize(1480, 900);

  setStyleSheet(R"(
    QMainWindow, QWidget#workspace { background: #eef2f3; color: #253238; }
    QWidget { font-family: "Segoe UI"; font-size: 9pt; }
    QWidget#sidebar { background: #182126; color: #edf4f3; }
    QLabel#brand { color: #ffffff; font-size: 16pt; font-weight: 700; }
    QLabel#platform { color: #8fc9c3; font-size: 8pt; font-weight: 600; }
    QListWidget#navigation { background: transparent; border: none; outline: none; color: #b9c6c9; }
    QListWidget#navigation::item { height: 42px; padding-left: 12px; border-left: 3px solid transparent; }
    QListWidget#navigation::item:hover { background: #243239; color: #ffffff; }
    QListWidget#navigation::item:selected { background: #293a40; color: #ffffff; border-left: 3px solid #36a69a; }
    QWidget#commandBar { background: #ffffff; border-bottom: 1px solid #d8e0e2; }
    QLabel#pageTitle { font-size: 17pt; font-weight: 650; color: #1f2b30; }
    QLabel[caption="true"] { color: #68767c; font-size: 8pt; font-weight: 600; }
    QLabel[metric="true"] { color: #203036; font-size: 12pt; font-weight: 650; }
    QFrame[card="true"], QGroupBox[surface="true"] { background: #ffffff; border: 1px solid #dce3e5; border-radius: 6px; }
    QGroupBox { margin-top: 12px; padding: 14px 12px 10px 12px; font-weight: 650; }
    QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #344349; }
    QPushButton { min-height: 30px; padding: 0 12px; border: 1px solid #cbd5d8; border-radius: 4px; background: #ffffff; color: #26353b; font-weight: 600; }
    QPushButton:hover { background: #f4f7f7; border-color: #9aabad; }
    QPushButton:disabled { color: #9ba6aa; background: #edf1f2; }
    QPushButton#applyButton { background: #e7f4f2; color: #126b63; border-color: #8dc8c1; }
    QPushButton#connectButton { background: #354b54; color: #ffffff; border-color: #354b54; }
    QPushButton#startButton[runState="start"] { background: #16724c; color: #ffffff; border-color: #16724c; min-width: 76px; }
    QPushButton#startButton[runState="stop"] { background: #cf6b2d; color: #ffffff; border-color: #cf6b2d; min-width: 76px; }
    QPushButton#emergencyButton { background: #b52e35; color: #ffffff; border-color: #b52e35; }
    QToolButton { min-width: 30px; min-height: 30px; border: 1px solid #cbd5d8; border-radius: 4px; background: #ffffff; }
    QToolButton:hover, QToolButton:checked { background: #e6f3f1; border-color: #67afa7; }
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { min-height: 28px; padding: 0 7px; background: #ffffff; border: 1px solid #cbd5d8; border-radius: 3px; }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #238b82; }
    QTabWidget::pane { border: 1px solid #d9e1e3; background: #ffffff; }
    QTabBar::tab { min-width: 106px; height: 31px; padding: 0 10px; background: #e8edef; color: #56656b; border: 1px solid #d8e0e2; }
    QTabBar::tab:selected { background: #ffffff; color: #176f68; border-top: 2px solid #25988d; }
    QLabel[statusKind="ready"] { background: #dff2e8; color: #12613f; border: 1px solid #a9d7bf; border-radius: 3px; padding: 3px 7px; font-weight: 600; }
    QLabel[statusKind="warn"] { background: #fff1d8; color: #8a5814; border: 1px solid #e6c37e; border-radius: 3px; padding: 3px 7px; font-weight: 600; }
    QLabel[statusKind="error"] { background: #f9e2e4; color: #932c34; border: 1px solid #dda4a9; border-radius: 3px; padding: 3px 7px; font-weight: 600; }
    QLabel[statusKind="neutral"] { background: #e8edef; color: #536168; border: 1px solid #ccd5d8; border-radius: 3px; padding: 3px 7px; font-weight: 600; }
    QPlainTextEdit { background: #11191d; color: #d5e0e2; border: 1px solid #2d3b41; font-family: Consolas; font-size: 9pt; }
    QStatusBar { background: #ffffff; color: #536168; border-top: 1px solid #d8e0e2; }
  )");

  auto* central = new QWidget(this);
  central->setObjectName("workspace");
  auto* shell = new QHBoxLayout(central);
  shell->setContentsMargins(0, 0, 0, 0);
  shell->setSpacing(0);

  auto* sidebar = new QWidget(central);
  sidebar->setObjectName("sidebar");
  sidebar->setFixedWidth(214);
  auto* sidebar_layout = new QVBoxLayout(sidebar);
  sidebar_layout->setContentsMargins(18, 20, 12, 16);
  sidebar_layout->setSpacing(6);
  auto* brand = new QLabel("FMCW LiDAR", sidebar);
  brand->setObjectName("brand");
  auto* platform = new QLabel(QString("%1  |  SIMULATOR").arg(platform_name_.toUpper()), sidebar);
  platform->setObjectName("platform");
  sidebar_layout->addWidget(brand);
  sidebar_layout->addWidget(platform);
  sidebar_layout->addSpacing(18);

  navigation_ = new QListWidget(sidebar);
  navigation_->setObjectName("navigation");
  navigation_->setIconSize(QSize(18, 18));
  const QList<QPair<QString, QStyle::StandardPixmap>> navigation_items = {
      {"Overview", QStyle::SP_ComputerIcon},
      {"Live View", QStyle::SP_MediaPlay},
      {"Digitizer", QStyle::SP_DriveHDIcon},
      {"Laser / EDFA", QStyle::SP_DialogYesButton},
      {"Scan / MCU", QStyle::SP_BrowserReload},
      {"Processing", QStyle::SP_FileDialogDetailedView},
      {"Storage / UDP", QStyle::SP_DriveNetIcon},
      {"System Log", QStyle::SP_FileIcon},
  };
  for (const auto& item : navigation_items) {
    auto* list_item = new QListWidgetItem(style()->standardIcon(item.second), item.first);
    navigation_->addItem(list_item);
  }
  sidebar_layout->addWidget(navigation_, 1);
  auto* version = new QLabel(QString("Core v%1\nQt Widgets MVP").arg(QString::fromStdString(versionString())), sidebar);
  version->setStyleSheet("color:#819196;font-size:8pt;");
  sidebar_layout->addWidget(version);

  auto* right = new QWidget(central);
  auto* right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(0, 0, 0, 0);
  right_layout->setSpacing(0);

  auto* command_bar = new QWidget(right);
  command_bar->setObjectName("commandBar");
  command_bar->setFixedHeight(64);
  auto* command_layout = new QHBoxLayout(command_bar);
  command_layout->setContentsMargins(16, 11, 16, 11);
  command_layout->setSpacing(7);
  profile_combo_ = new QComboBox(command_bar);
  profile_combo_->setMinimumWidth(180);
  profile_combo_->setMaximumWidth(230);
  load_button_ = new QToolButton(command_bar);
  load_button_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  load_button_->setToolTip("Load profile");
  save_button_ = new QToolButton(command_bar);
  save_button_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_button_->setToolTip("Save profile");
  apply_button_ = new QPushButton("Apply", command_bar);
  apply_button_->setObjectName("applyButton");
  validation_label_ = new QLabel("CHECKING", command_bar);
  validation_label_->setProperty("statusKind", "neutral");
  raw_indicator_ = new QLabel("RAW OFF", command_bar);
  raw_indicator_->setProperty("statusKind", "neutral");
  udp_indicator_ = new QLabel("UDP OFF", command_bar);
  udp_indicator_->setProperty("statusKind", "neutral");
  connect_button_ = new QPushButton("Connect", command_bar);
  connect_button_->setObjectName("connectButton");
  start_stop_button_ = new QPushButton("START", command_bar);
  start_stop_button_->setObjectName("startButton");
  start_stop_button_->setProperty("runState", "start");
  emergency_button_ = new QPushButton("E-STOP", command_bar);
  emergency_button_->setObjectName("emergencyButton");
  emergency_button_->setToolTip("Emergency stop all scan, acquisition, and optical output");
  command_layout->addWidget(profile_combo_);
  command_layout->addWidget(load_button_);
  command_layout->addWidget(save_button_);
  command_layout->addWidget(apply_button_);
  command_layout->addWidget(validation_label_);
  command_layout->addWidget(raw_indicator_);
  command_layout->addWidget(udp_indicator_);
  command_layout->addStretch(1);
  command_layout->addWidget(connect_button_);
  command_layout->addWidget(start_stop_button_);
  command_layout->addWidget(emergency_button_);

  pages_ = new QStackedWidget(right);
  pages_->addWidget(makePage("Overview", buildOverviewPage()));
  pages_->addWidget(makePage("Live View", buildLivePage()));
  pages_->addWidget(makePage("Digitizer", buildDigitizerPage()));
  pages_->addWidget(makePage("Laser / EDFA", buildLaserEdfaPage()));
  pages_->addWidget(makePage("Scan / MCU", buildScanMcuPage()));
  pages_->addWidget(makePage("Processing", buildProcessingPage()));
  pages_->addWidget(makePage("Storage / UDP", buildStorageUdpPage()));
  pages_->addWidget(makePage("System Log", buildLogPage()));
  right_layout->addWidget(command_bar);
  right_layout->addWidget(pages_, 1);

  shell->addWidget(sidebar);
  shell->addWidget(right, 1);
  setCentralWidget(central);

  runtime_state_label_ = new QLabel("DISCONNECTED", this);
  runtime_state_label_->setProperty("statusKind", "neutral");
  statusBar()->addWidget(runtime_state_label_);
  statusBar()->showMessage("Simulator runtime | Up-chirp trigger | Full-period acquisition", 0);

  controller_ = new ApplicationController(platform_name_, this);
  loadConfigToControls(config_);
  connectUi();
  navigation_->setCurrentRow(0);
  validateControls();
  controller_->applyConfig(config_);
}

QWidget* MainWindow::makePage(QString title, QWidget* content) {
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(20, 16, 20, 18);
  layout->setSpacing(12);
  auto* heading = new QLabel(std::move(title), page);
  heading->setObjectName("pageTitle");
  layout->addWidget(heading);
  layout->addWidget(content, 1);
  return page;
}

QWidget* MainWindow::buildOverviewPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto* grid = new QGridLayout;
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(10);
  grid->addWidget(statusCard("DIGITIZER", overview_digitizer_, content), 0, 0);
  grid->addWidget(statusCard("LASER / EDFA", overview_edfa_, content), 0, 1);
  grid->addWidget(statusCard("SCAN / MCU", overview_mcu_, content), 0, 2);
  grid->addWidget(statusCard("PROCESSING", overview_processing_, content), 0, 3);
  grid->addWidget(statusCard("FRAMES", overview_frames_, content), 1, 0);
  grid->addWidget(statusCard("QUEUES", overview_queues_, content), 1, 1);
  grid->addWidget(statusCard("FFT LATENCY", overview_latency_, content), 1, 2);
  grid->addWidget(statusCard("RECORDING", overview_recording_, content), 1, 3);
  for (int column = 0; column < 4; ++column) {
    grid->setColumnStretch(column, 1);
  }
  layout->addLayout(grid);

  auto* state_group = groupBox("Session State", content);
  auto* state_layout = new QVBoxLayout(state_group);
  overview_detail_ = new QLabel("Waiting for runtime initialization", state_group);
  overview_detail_->setWordWrap(true);
  overview_detail_->setMinimumHeight(48);
  state_layout->addWidget(overview_detail_);
  layout->addWidget(state_group);
  layout->addStretch(1);
  return content;
}

QWidget* MainWindow::buildLivePage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  auto* tools = new QHBoxLayout;
  tools->addStretch(1);
  auto* auto_range = new QToolButton(content);
  auto_range->setCheckable(true);
  auto_range->setChecked(true);
  auto_range->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  auto_range->setToolTip("Auto range plots");
  auto* manual_range = new QToolButton(content);
  manual_range->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  manual_range->setToolTip("Set manual Y or Z range");
  freeze_button_ = new QToolButton(content);
  freeze_button_->setCheckable(true);
  freeze_button_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
  freeze_button_->setToolTip("Freeze plot display");
  auto* save_view = new QToolButton(content);
  save_view->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_view->setToolTip("Save current view as PNG");
  connect(save_view, &QToolButton::clicked, this, &MainWindow::saveCurrentView);
  tools->addWidget(auto_range);
  tools->addWidget(manual_range);
  tools->addWidget(freeze_button_);
  tools->addWidget(save_view);
  layout->addLayout(tools);

  live_tabs_ = new QTabWidget(content);
  time_plot_ = new LinePlotWidget(live_tabs_);
  time_plot_->setTitle("Full-period Time Domain");
  time_plot_->setAxisLabels("Sample", "Amplitude");
  fft_plot_ = new LinePlotWidget(live_tabs_);
  fft_plot_->setTitle("UP / DOWN FFT Spectrum");
  fft_plot_->setAxisLabels("FFT bin", "Magnitude (dB)");

  auto* peak_page = new QWidget(live_tabs_);
  auto* peak_layout = new QVBoxLayout(peak_page);
  peak_layout->setContentsMargins(0, 0, 0, 0);
  peak_layout->setSpacing(0);
  peak_index_plot_ = new LinePlotWidget(peak_page);
  peak_index_plot_->setTitle("Peak Index vs A-scan");
  peak_index_plot_->setAxisLabels("A-scan", "Peak bin");
  peak_value_plot_ = new LinePlotWidget(peak_page);
  peak_value_plot_->setTitle("Peak Value vs A-scan");
  peak_value_plot_->setAxisLabels("A-scan", "Magnitude (dB)");
  auto* peak_splitter = new QSplitter(Qt::Vertical, peak_page);
  peak_splitter->addWidget(peak_index_plot_);
  peak_splitter->addWidget(peak_value_plot_);
  peak_layout->addWidget(peak_splitter);

  distance_plot_ = new LinePlotWidget(live_tabs_);
  distance_plot_->setTitle("Distance / Velocity vs A-scan");
  distance_plot_->setAxisLabels("A-scan", "Measurement");
  bscan_plot_ = new HeatmapWidget(live_tabs_);
  live_tabs_->addTab(time_plot_, "Time Domain");
  live_tabs_->addTab(fft_plot_, "FFT");
  live_tabs_->addTab(peak_page, "Peak Analysis");
  live_tabs_->addTab(distance_plot_, "Distance / Velocity");
  live_tabs_->addTab(bscan_plot_, "B-scan");
  layout->addWidget(live_tabs_, 1);
  connect(auto_range, &QToolButton::toggled, content, [this](bool enabled) {
    const QList<LinePlotWidget*> line_plots = {time_plot_, fft_plot_, peak_index_plot_, peak_value_plot_, distance_plot_};
    for (auto* plot : line_plots) {
      plot->setAutoRange(enabled);
    }
    bscan_plot_->setAutoRange(enabled);
  });
  connect(manual_range, &QToolButton::clicked, this, [this, auto_range] {
    LinePlotWidget* line_plot = nullptr;
    HeatmapWidget* heatmap = nullptr;
    if (live_tabs_->currentIndex() == 0) {
      line_plot = time_plot_;
    } else if (live_tabs_->currentIndex() == 1) {
      line_plot = fft_plot_;
    } else if (live_tabs_->currentIndex() == 2) {
      bool selected = false;
      const auto name = QInputDialog::getItem(this, "Manual range", "Plot",
                                               {"Peak Index", "Peak Value"}, 0, false, &selected);
      if (!selected) {
        return;
      }
      line_plot = name == "Peak Index" ? peak_index_plot_ : peak_value_plot_;
    } else if (live_tabs_->currentIndex() == 3) {
      line_plot = distance_plot_;
    } else {
      heatmap = bscan_plot_;
    }
    const auto current = line_plot != nullptr ? line_plot->currentRange() : heatmap->currentRange();
    bool accepted = false;
    const auto minimum = QInputDialog::getDouble(this, "Manual range", "Minimum",
                                                  current.first, -1.0e12, 1.0e12, 6, &accepted);
    if (!accepted) {
      return;
    }
    const auto maximum = QInputDialog::getDouble(this, "Manual range", "Maximum",
                                                  current.second, -1.0e12, 1.0e12, 6, &accepted);
    if (!accepted) {
      return;
    }
    if (minimum >= maximum) {
      QMessageBox::warning(this, "Manual range", "Maximum must be greater than minimum.");
      return;
    }
    auto_range->setChecked(false);
    if (line_plot != nullptr) {
      line_plot->setManualRange(static_cast<float>(minimum), static_cast<float>(maximum));
    } else {
      heatmap->setManualRange(static_cast<float>(minimum), static_cast<float>(maximum));
    }
  });
  return content;
}

QWidget* MainWindow::buildDigitizerPage() {
  auto* content = new QWidget;
  auto* layout = new QGridLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto* board = groupBox("Alazar Board Setup", content);
  auto* board_form = new QFormLayout(board);
  digitizer_channel_ = new QComboBox(board);
  digitizer_channel_->addItems({"Channel A", "Channel B"});
  sample_rate_ = new QDoubleSpinBox(board);
  sample_rate_->setRange(1.0, 5000.0);
  sample_rate_->setDecimals(1);
  sample_rate_->setSuffix(" MS/s");
  sample_point_ = new QSpinBox(board);
  sample_point_->setRange(256, 16 * 1024 * 1024);
  sample_point_->setSingleStep(256);
  input_range_ = new QDoubleSpinBox(board);
  input_range_->setRange(0.02, 20.0);
  input_range_->setDecimals(3);
  input_range_->setSuffix(" V");
  coupling_ = new QComboBox(board);
  coupling_->addItems({"DC", "AC"});
  board_form->addRow("Input channel", digitizer_channel_);
  board_form->addRow("Sampling rate", sample_rate_);
  board_form->addRow("Sample points", sample_point_);
  board_form->addRow("Input range", input_range_);
  board_form->addRow("Coupling", coupling_);

  auto* dma = groupBox("DMA / Trigger", content);
  auto* dma_form = new QFormLayout(dma);
  records_per_buffer_ = new QSpinBox(dma);
  records_per_buffer_->setRange(1, 4096);
  dma_buffer_count_ = new QSpinBox(dma);
  dma_buffer_count_->setRange(2, 128);
  trigger_level_ = new QDoubleSpinBox(dma);
  trigger_level_->setRange(0.0, 100.0);
  trigger_level_->setSuffix(" %");
  auto* trigger_mode = new QLabel("External rising | Trigger on UP only | Capture full period", dma);
  trigger_mode->setWordWrap(true);
  trigger_mode->setProperty("statusKind", "ready");
  dma_form->addRow("Records / buffer", records_per_buffer_);
  dma_form->addRow("DMA buffers", dma_buffer_count_);
  dma_form->addRow("Trigger level", trigger_level_);
  dma_form->addRow("Trigger contract", trigger_mode);
  layout->addWidget(board, 0, 0);
  layout->addWidget(dma, 0, 1);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  return wrapInScrollArea(content);
}

QWidget* MainWindow::buildLaserEdfaPage() {
  auto* content = new QWidget;
  auto* layout = new QGridLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto* laser = groupBox("Laser Specification", content);
  auto* laser_form = new QFormLayout(laser);
  wavelength_ = new QDoubleSpinBox(laser);
  wavelength_->setRange(200.0, 3000.0);
  wavelength_->setDecimals(2);
  wavelength_->setSuffix(" nm");
  sweep_bandwidth_ = new QDoubleSpinBox(laser);
  sweep_bandwidth_->setRange(0.001, 1000.0);
  sweep_bandwidth_->setDecimals(3);
  sweep_bandwidth_->setSuffix(" GHz");
  chirp_period_ = new QDoubleSpinBox(laser);
  chirp_period_->setRange(0.01, 100000.0);
  chirp_period_->setDecimals(3);
  chirp_period_->setSuffix(" us");
  laser_power_ = new QDoubleSpinBox(laser);
  laser_power_->setRange(0.0, 10000.0);
  laser_power_->setSuffix(" mW");
  laser_form->addRow("Wavelength", wavelength_);
  laser_form->addRow("Sweep bandwidth", sweep_bandwidth_);
  laser_form->addRow("Full chirp period", chirp_period_);
  laser_form->addRow("Laser power", laser_power_);

  auto* edfa = groupBox("Optional EDFA", content);
  auto* edfa_form = new QFormLayout(edfa);
  edfa_mode_ = new QComboBox(edfa);
  edfa_mode_->addItems({"None / Bypass", "Manual", "Controlled"});
  edfa_port_ = new QLineEdit(edfa);
  edfa_port_->setPlaceholderText(platform_name_ == "Windows" ? "COM3" : "/dev/ttyUSB0");
  edfa_control_mode_ = new QComboBox(edfa);
  edfa_control_mode_->addItems({"APC", "ACC", "AGC"});
  edfa_setpoint_ = new QDoubleSpinBox(edfa);
  edfa_setpoint_->setRange(0.0, 23.0);
  edfa_setpoint_->setDecimals(1);
  edfa_setpoint_->setSuffix(" dBm");
  edfa_warmup_ = new QSpinBox(edfa);
  edfa_warmup_->setRange(0, 60000);
  edfa_warmup_->setSuffix(" ms");
  edfa_output_button_ = new QPushButton("Enable Output", edfa);
  edfa_output_button_->setToolTip("Independent optical output safety control");
  edfa_form->addRow("Mode", edfa_mode_);
  edfa_form->addRow("Serial port", edfa_port_);
  edfa_form->addRow("Control", edfa_control_mode_);
  edfa_form->addRow("Output setpoint", edfa_setpoint_);
  edfa_form->addRow("Warm-up", edfa_warmup_);
  edfa_form->addRow("Optical output", edfa_output_button_);
  layout->addWidget(laser, 0, 0);
  layout->addWidget(edfa, 0, 1);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  return wrapInScrollArea(content);
}

QWidget* MainWindow::buildScanMcuPage() {
  auto* content = new QWidget;
  auto* layout = new QGridLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto* geometry = groupBox("Scan Geometry", content);
  auto* geometry_form = new QFormLayout(geometry);
  auto angleSpin = [geometry] {
    auto* spin = new QDoubleSpinBox(geometry);
    spin->setRange(-180.0, 180.0);
    spin->setDecimals(2);
    spin->setSuffix(" deg");
    return spin;
  };
  x_start_ = angleSpin();
  x_end_ = angleSpin();
  y_start_ = angleSpin();
  y_end_ = angleSpin();
  x_pixels_ = new QSpinBox(geometry);
  x_pixels_->setRange(2, 15000);
  y_lines_ = new QSpinBox(geometry);
  y_lines_->setRange(2, 10000);
  line_time_ = new QDoubleSpinBox(geometry);
  line_time_->setRange(0.001, 10000.0);
  line_time_->setDecimals(3);
  line_time_->setSuffix(" ms");
  bidirectional_ = new QCheckBox("Bidirectional raster", geometry);
  geometry_form->addRow("X start", x_start_);
  geometry_form->addRow("X end", x_end_);
  geometry_form->addRow("Y start", y_start_);
  geometry_form->addRow("Y end", y_end_);
  geometry_form->addRow("X pixels / A-scans", x_pixels_);
  geometry_form->addRow("Y lines / B-scans", y_lines_);
  geometry_form->addRow("Line time", line_time_);
  geometry_form->addRow("Scan direction", bidirectional_);

  auto* mcu = groupBox("MCU Waveform", content);
  auto* mcu_form = new QFormLayout(mcu);
  mcu_enabled_ = new QCheckBox("Use MCU scan and trigger controller", mcu);
  mcu_port_ = new QLineEdit(mcu);
  mcu_port_->setPlaceholderText(platform_name_ == "Windows" ? "COM4" : "/dev/ttyACM0");
  upload_waveform_button_ = new QPushButton("Upload Waveform", mcu);
  mcu_waveform_state_ = new QLabel("MCU bypass active", mcu);
  mcu_waveform_state_->setProperty("statusKind", "neutral");
  mcu_form->addRow("Controller", mcu_enabled_);
  mcu_form->addRow("Serial port", mcu_port_);
  mcu_form->addRow("Waveform", upload_waveform_button_);
  mcu_form->addRow("Readiness", mcu_waveform_state_);
  layout->addWidget(geometry, 0, 0);
  layout->addWidget(mcu, 0, 1);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  return wrapInScrollArea(content);
}

QWidget* MainWindow::buildProcessingPage() {
  auto* content = new QWidget;
  auto* layout = new QGridLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  auto* fft = groupBox("FFT / Preprocessing", content);
  auto* fft_form = new QFormLayout(fft);
  fft_backend_ = new QComboBox(fft);
  fft_backend_->addItems({"FFTW (CPU)", "CUDA cuFFT"});
  window_function_ = new QComboBox(fft);
  window_function_->addItems({"Hann", "Hamming", "Blackman", "Rectangular"});
  dc_removal_ = new QCheckBox("Remove DC component", fft);
  normalize_ = new QCheckBox("Normalize segment", fft);
  fft_length_ = new QSpinBox(fft);
  fft_length_->setRange(256, 1048576);
  fft_length_->setSingleStep(256);
  fft_form->addRow("Backend", fft_backend_);
  fft_form->addRow("Window", window_function_);
  fft_form->addRow("FFT length", fft_length_);
  fft_form->addRow("DC removal", dc_removal_);
  fft_form->addRow("Normalization", normalize_);

  auto* peak = groupBox("Peak Detection / Tracking", content);
  auto* peak_form = new QFormLayout(peak);
  peak_threshold_ = new QDoubleSpinBox(peak);
  peak_threshold_->setRange(-180.0, 20.0);
  peak_threshold_->setDecimals(1);
  peak_threshold_->setSuffix(" dB");
  peak_start_ = new QSpinBox(peak);
  peak_start_->setRange(0, 1048576);
  peak_end_ = new QSpinBox(peak);
  peak_end_->setRange(1, 1048576);
  peak_tracking_ = new QCheckBox("Track peak continuity by A-scan", peak);
  peak_delta_ = new QSpinBox(peak);
  peak_delta_->setRange(1, 1048576);
  reacquire_width_ = new QSpinBox(peak);
  reacquire_width_->setRange(1, 1048576);
  lost_policy_ = new QComboBox(peak);
  lost_policy_->addItems({"Hold last (invalid)", "Reacquire", "Stop acquisition"});
  auto* update_runtime = new QPushButton("Apply Processing", peak);
  peak_form->addRow("Peak threshold", peak_threshold_);
  peak_form->addRow("Search start bin", peak_start_);
  peak_form->addRow("Search end bin", peak_end_);
  peak_form->addRow("Tracking", peak_tracking_);
  peak_form->addRow("Max delta", peak_delta_);
  peak_form->addRow("Reacquire width", reacquire_width_);
  peak_form->addRow("Lost policy", lost_policy_);
  peak_form->addRow("Runtime", update_runtime);

  auto* segmentation = groupBox("Chirp Segmentation Snapshot", content);
  auto* segmentation_layout = new QVBoxLayout(segmentation);
  auto* controls = new QHBoxLayout;
  auto segmentSpin = [segmentation] {
    auto* spin = new QSpinBox(segmentation);
    spin->setRange(0, 16 * 1024 * 1024);
    spin->setMaximumWidth(105);
    return spin;
  };
  up_start_ = segmentSpin();
  up_end_ = segmentSpin();
  down_start_ = segmentSpin();
  down_end_ = segmentSpin();
  guard_samples_ = segmentSpin();
  auto addControl = [&controls](const QString& label, QWidget* widget) {
    controls->addWidget(new QLabel(label));
    controls->addWidget(widget);
  };
  addControl("UP start", up_start_);
  addControl("UP end", up_end_);
  addControl("DOWN start", down_start_);
  addControl("DOWN end", down_end_);
  addControl("Guard", guard_samples_);
  controls->addStretch(1);
  auto* capture = new QPushButton("Capture Snapshot", segmentation);
  controls->addWidget(capture);
  segmentation_state_ = new QLabel("No frozen frame", segmentation);
  segmentation_state_->setProperty("statusKind", "neutral");
  segmentation_plot_ = new SegmentationPlotWidget(segmentation);
  segmentation_layout->addLayout(controls);
  segmentation_layout->addWidget(segmentation_state_);
  segmentation_layout->addWidget(segmentation_plot_, 1);
  layout->addWidget(fft, 0, 0);
  layout->addWidget(peak, 0, 1);
  layout->addWidget(segmentation, 1, 0, 1, 2);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  connect(capture, &QPushButton::clicked, this, [this] { controller_->captureSegmentationSnapshot(); });
  connect(update_runtime, &QPushButton::clicked, this, [this] {
    if (!validateControls(true)) {
      return;
    }
    const auto updated = configFromControls();
    if (runtime_status_.running) {
      controller_->updateProcessing(updated.processing);
    } else {
      controller_->applyConfig(updated);
    }
  });
  return content;
}

QWidget* MainWindow::buildStorageUdpPage() {
  auto* content = new QWidget;
  auto* layout = new QGridLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto* storage = groupBox("Session Storage", content);
  auto* storage_form = new QFormLayout(storage);
  raw_enabled_ = new QCheckBox("Write full-period raw frames", storage);
  processed_enabled_ = new QCheckBox("Write processed measurements", storage);
  output_directory_ = new QLineEdit(storage);
  auto* path_row = new QWidget(storage);
  auto* path_layout = new QHBoxLayout(path_row);
  path_layout->setContentsMargins(0, 0, 0, 0);
  auto* browse = new QToolButton(path_row);
  browse->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
  browse->setToolTip("Select session output directory");
  path_layout->addWidget(output_directory_, 1);
  path_layout->addWidget(browse);
  storage_queue_ = new QSpinBox(storage);
  storage_queue_->setRange(1, 65536);
  split_size_ = new QDoubleSpinBox(storage);
  split_size_->setRange(0.001, 1024.0);
  split_size_->setDecimals(3);
  split_size_->setSuffix(" GB");
  storage_form->addRow("Raw", raw_enabled_);
  storage_form->addRow("Processed", processed_enabled_);
  storage_form->addRow("Output directory", path_row);
  storage_form->addRow("Writer queue", storage_queue_);
  storage_form->addRow("Split size", split_size_);

  auto* udp = groupBox("UDP Output", content);
  auto* udp_form = new QFormLayout(udp);
  udp_enabled_ = new QCheckBox("Configure UDP point output", udp);
  udp_ip_ = new QLineEdit(udp);
  udp_port_ = new QSpinBox(udp);
  udp_port_->setRange(1, 65535);
  udp_points_ = new QSpinBox(udp);
  udp_points_->setRange(1, 65535);
  auto* packet_format = new QLabel("FMCW point packet v1 | CONFIG ONLY", udp);
  packet_format->setWordWrap(true);
  packet_format->setProperty("statusKind", "neutral");
  udp_form->addRow("Output", udp_enabled_);
  udp_form->addRow("Target IPv4", udp_ip_);
  udp_form->addRow("Target port", udp_port_);
  udp_form->addRow("Points / packet", udp_points_);
  udp_form->addRow("Format", packet_format);
  layout->addWidget(storage, 0, 0);
  layout->addWidget(udp, 0, 1);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  connect(browse, &QToolButton::clicked, this, [this] {
    const auto path = QFileDialog::getExistingDirectory(this, "Session output directory", output_directory_->text());
    if (!path.isEmpty()) {
      output_directory_->setText(path);
    }
  });
  return wrapInScrollArea(content);
}

QWidget* MainWindow::buildLogPage() {
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  auto* toolbar = new QHBoxLayout;
  log_filter_ = new QComboBox(content);
  log_filter_->addItems({"All levels", "INFO", "WARNING", "ERROR", "CRITICAL"});
  auto* clear = new QPushButton("Clear", content);
  toolbar->addWidget(new QLabel("Filter"));
  toolbar->addWidget(log_filter_);
  toolbar->addStretch(1);
  toolbar->addWidget(clear);
  log_view_ = new QPlainTextEdit(content);
  log_view_->setReadOnly(true);
  layout->addLayout(toolbar);
  layout->addWidget(log_view_, 1);
  connect(log_filter_, &QComboBox::currentIndexChanged, this, [this] { rebuildLog(); });
  connect(clear, &QPushButton::clicked, this, [this] {
    log_entries_.clear();
    log_view_->clear();
  });
  return content;
}

void MainWindow::connectUi() {
  connect(navigation_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
  connect(load_button_, &QToolButton::clicked, this, &MainWindow::loadProfile);
  connect(save_button_, &QToolButton::clicked, this, &MainWindow::saveProfile);
  connect(apply_button_, &QPushButton::clicked, this, &MainWindow::applyProfile);
  connect(connect_button_, &QPushButton::clicked, this, [this] {
    if (runtime_status_.connected) {
      controller_->disconnectSystem();
      return;
    }
    if (validateControls(true)) {
      controller_->connectSystem(configFromControls());
    }
  });
  connect(start_stop_button_, &QPushButton::clicked, this, [this] {
    if (runtime_status_.running) {
      controller_->stopSystem();
      return;
    }
    if (validateControls(true)) {
      controller_->startSystem(configFromControls());
    }
  });
  connect(emergency_button_, &QPushButton::clicked, this, [this] {
    const auto answer = QMessageBox::warning(this, "Emergency stop",
        "Immediately stop MCU trigger, digitizer acquisition, and controlled EDFA output?",
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Yes) {
      controller_->emergencyStop();
    }
  });
  connect(edfa_output_button_, &QPushButton::clicked, this, [this] {
    controller_->setEdfaOutput(!runtime_status_.edfa_output_enabled);
  });
  connect(upload_waveform_button_, &QPushButton::clicked, controller_, &ApplicationController::uploadMcuWaveform);
  connect(freeze_button_, &QToolButton::toggled, this, [this](bool checked) {
    freeze_live_ = checked;
    freeze_button_->setIcon(style()->standardIcon(checked ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    freeze_button_->setToolTip(checked ? "Resume live plot display" : "Freeze plot display");
  });

  connect(controller_, &ApplicationController::statusChanged, this, &MainWindow::updateStatus);
  connect(controller_, &ApplicationController::logMessage, this, &MainWindow::appendLog);
  connect(controller_, &ApplicationController::commandFailed, this, [this](const QString& command, const QString& message) {
    statusBar()->showMessage(QString("%1 failed: %2").arg(command, message), 8000);
  });
  connect(controller_, &ApplicationController::commandCompleted, this,
          [this](const QString& command, const QString& message) {
            statusBar()->showMessage(QString("%1: %2").arg(command, message), 5000);
            if (command == "Apply configuration" || command == "Connect" || command == "Start") {
              config_ = configFromControls();
              config_dirty_ = false;
              validateControls();
            }
          });
  connect(controller_, &ApplicationController::waveformReady, this, [this](WaveformSnapshotPtr snapshot) {
    if (freeze_live_ || snapshot == nullptr) {
      return;
    }
    time_plot_->setSeries({{"Full period", qVector(snapshot->normalized_samples), QColor("#167a86")}});
  });
  connect(controller_, &ApplicationController::fftReady, this, [this](FftSnapshotPtr snapshot) {
    if (freeze_live_ || snapshot == nullptr) {
      return;
    }
    fft_plot_->setSeries({{"UP", qVector(snapshot->up_magnitude_db), QColor("#188266")},
                          {"DOWN", qVector(snapshot->down_magnitude_db), QColor("#d06432")}});
  });
  connect(controller_, &ApplicationController::scanLineReady, this, [this](ScanLineSnapshotPtr snapshot) {
    if (freeze_live_ || snapshot == nullptr) {
      return;
    }
    peak_index_plot_->setSeries({{"UP", qVector(snapshot->up_peak_index), QColor("#188266")},
                                 {"DOWN", qVector(snapshot->down_peak_index), QColor("#d06432")}});
    peak_value_plot_->setSeries({{"UP", qVector(snapshot->up_peak_value_db), QColor("#188266")},
                                 {"DOWN", qVector(snapshot->down_peak_value_db), QColor("#d06432")}});
    auto distance = qVector(snapshot->distance_m);
    auto velocity = qVector(snapshot->velocity_mps);
    for (int index = 0; index < distance.size() && index < static_cast<int>(snapshot->valid.size()); ++index) {
      if (snapshot->valid[static_cast<std::size_t>(index)] == 0U) {
        distance[index] = std::numeric_limits<float>::quiet_NaN();
        velocity[index] = std::numeric_limits<float>::quiet_NaN();
      }
    }
    distance_plot_->setSeries({{"Distance (m)", std::move(distance), QColor("#13737f")},
                                {"Velocity (m/s)", std::move(velocity), QColor("#ad4e61")}});
  });
  connect(controller_, &ApplicationController::bscanReady, this, [this](BScanSnapshotPtr snapshot) {
    if (!freeze_live_ && snapshot != nullptr) {
      bscan_plot_->setData(snapshot->width, snapshot->height, snapshot->z_m, snapshot->valid,
                           snapshot->completed_lines);
    }
  });
  connect(controller_, &ApplicationController::segmentationSnapshotReady, this,
          [this](WaveformSnapshotPtr snapshot) {
            segmentation_plot_->setSnapshot(snapshot);
            segmentation_plot_->setSegments({static_cast<std::uint32_t>(up_start_->value()),
                                              static_cast<std::uint32_t>(up_end_->value())},
                                             {static_cast<std::uint32_t>(down_start_->value()),
                                              static_cast<std::uint32_t>(down_end_->value())},
                                             static_cast<std::uint32_t>(guard_samples_->value()));
            segmentation_state_->setText(QString("Frozen frame %1 | overlay follows current controls").arg(snapshot->frame_id));
            segmentation_state_->setProperty("statusKind", "ready");
            repolish(segmentation_state_);
          });

  const QList<QObject*> config_controls = {
      digitizer_channel_, sample_rate_, sample_point_, records_per_buffer_, dma_buffer_count_, input_range_, coupling_,
      trigger_level_, wavelength_, sweep_bandwidth_, chirp_period_, laser_power_, edfa_mode_, edfa_port_,
      edfa_control_mode_, edfa_setpoint_, edfa_warmup_, x_start_, x_end_, y_start_, y_end_, x_pixels_, y_lines_,
      line_time_, bidirectional_, mcu_enabled_, mcu_port_, fft_backend_, window_function_, dc_removal_, normalize_,
      peak_threshold_, peak_start_, peak_end_, peak_tracking_, peak_delta_, reacquire_width_, lost_policy_,
      up_start_, up_end_, down_start_, down_end_, guard_samples_, fft_length_, raw_enabled_, processed_enabled_,
      output_directory_, storage_queue_, split_size_, udp_enabled_, udp_ip_, udp_port_, udp_points_};
  for (auto* control : config_controls) {
    if (auto* spin = qobject_cast<QSpinBox*>(control)) {
      connect(spin, &QSpinBox::valueChanged, this, [this] { markDirty(); });
    } else if (auto* double_spin = qobject_cast<QDoubleSpinBox*>(control)) {
      connect(double_spin, &QDoubleSpinBox::valueChanged, this, [this] { markDirty(); });
    } else if (auto* combo = qobject_cast<QComboBox*>(control)) {
      connect(combo, &QComboBox::currentIndexChanged, this, [this] { markDirty(); });
    } else if (auto* check = qobject_cast<QCheckBox*>(control)) {
      connect(check, &QCheckBox::toggled, this, [this] { markDirty(); });
    } else if (auto* edit = qobject_cast<QLineEdit*>(control)) {
      connect(edit, &QLineEdit::textChanged, this, [this] { markDirty(); });
    }
  }
}

void MainWindow::markDirty() {
  config_dirty_ = true;
  raw_indicator_->setText(raw_enabled_->isChecked() ? "RAW ON" : "RAW OFF");
  raw_indicator_->setProperty("statusKind", raw_enabled_->isChecked() ? "ready" : "neutral");
  udp_indicator_->setText(udp_enabled_->isChecked() ? "UDP CFG" : "UDP OFF");
  udp_indicator_->setProperty("statusKind", udp_enabled_->isChecked() ? "ready" : "neutral");
  repolish(raw_indicator_);
  repolish(udp_indicator_);
  segmentation_plot_->setSegments({static_cast<std::uint32_t>(up_start_->value()),
                                    static_cast<std::uint32_t>(up_end_->value())},
                                   {static_cast<std::uint32_t>(down_start_->value()),
                                    static_cast<std::uint32_t>(down_end_->value())},
                                   static_cast<std::uint32_t>(guard_samples_->value()));
  validateControls();
}

bool MainWindow::validateControls(bool show_dialog) {
  const auto candidate = configFromControls();
  const auto result = ConfigValidator::validate(candidate);
  QString tooltip;
  int errors = 0;
  int warnings = 0;
  for (const auto& issue : result.issues) {
    if (!tooltip.isEmpty()) {
      tooltip += "\n";
    }
    tooltip += QString("%1: %2").arg(QString::fromStdString(issue.path), QString::fromStdString(issue.message));
    if (issue.severity == ValidationSeverity::Error) {
      ++errors;
    } else if (issue.severity == ValidationSeverity::Warning) {
      ++warnings;
    }
  }
  if (errors > 0) {
    validation_label_->setText(QString("%1 ERROR").arg(errors));
    validation_label_->setProperty("statusKind", "error");
  } else if (warnings > 0) {
    validation_label_->setText(config_dirty_ ? QString("VALID | %1 WARN | PENDING").arg(warnings)
                                             : QString("VALID | %1 WARN").arg(warnings));
    validation_label_->setProperty("statusKind", "warn");
  } else {
    validation_label_->setText(config_dirty_ ? "VALID | PENDING" : "VALID");
    validation_label_->setProperty("statusKind", "ready");
  }
  validation_label_->setToolTip(tooltip);
  repolish(validation_label_);
  start_stop_button_->setEnabled(runtime_status_.running || errors == 0);
  apply_button_->setEnabled(!runtime_status_.running && errors == 0);
  if (show_dialog && errors > 0) {
    QMessageBox::warning(this, "Configuration validation", tooltip);
  }
  return errors == 0;
}

SystemConfig MainWindow::configFromControls() const {
  auto config = config_;
  config.profile.name = profile_combo_->currentText().toStdString();
  config.digitizer.channel = digitizer_channel_->currentIndex() == 0 ? DigitizerChannel::A : DigitizerChannel::B;
  config.digitizer.sample_rate_hz = sample_rate_->value() * 1.0e6;
  config.digitizer.sample_point = static_cast<std::uint32_t>(sample_point_->value());
  config.digitizer.records_per_buffer = static_cast<std::uint32_t>(records_per_buffer_->value());
  config.digitizer.dma_buffer_count = static_cast<std::uint32_t>(dma_buffer_count_->value());
  config.digitizer.input_range_volts = input_range_->value();
  config.digitizer.coupling = coupling_->currentIndex() == 0 ? Coupling::Dc : Coupling::Ac;
  config.digitizer.trigger_level_percent = trigger_level_->value();
  config.digitizer.post_trigger_samples = config.digitizer.sample_point > config.digitizer.pre_trigger_samples
      ? config.digitizer.sample_point - config.digitizer.pre_trigger_samples
      : 0U;
  config.laser.wavelength_nm = wavelength_->value();
  config.laser.sweep_bandwidth_hz = sweep_bandwidth_->value() * 1.0e9;
  config.laser.chirp_period_us = chirp_period_->value();
  config.laser.laser_power_mw = laser_power_->value();
  config.edfa.mode = static_cast<EdfaMode>(edfa_mode_->currentIndex());
  config.edfa.port = edfa_port_->text().trimmed().toStdString();
  config.edfa.control_mode = edfa_control_mode_->currentIndex() == 0 ? EdfaControlMode::Apc
      : edfa_control_mode_->currentIndex() == 1 ? EdfaControlMode::Acc : EdfaControlMode::Agc;
  config.edfa.output_setpoint.value = edfa_setpoint_->value();
  config.edfa.output_setpoint.unit = OpticalPowerUnit::Dbm;
  config.edfa.warmup_delay_ms = static_cast<std::uint32_t>(edfa_warmup_->value());
  config.scan.x_start_deg = x_start_->value();
  config.scan.x_end_deg = x_end_->value();
  config.scan.y_start_deg = y_start_->value();
  config.scan.y_end_deg = y_end_->value();
  config.scan.x_pixel_count = static_cast<std::uint32_t>(x_pixels_->value());
  config.scan.y_line_count = static_cast<std::uint32_t>(y_lines_->value());
  config.scan.line_time_ms = line_time_->value();
  config.scan.bidirectional = bidirectional_->isChecked();
  config.digitizer.a_scan_count = config.scan.x_pixel_count;
  config.digitizer.b_scan_count = config.scan.y_line_count;
  config.mcu.enabled = mcu_enabled_->isChecked();
  config.mcu.port = mcu_port_->text().trimmed().toStdString();
  config.processing.fft_backend = fft_backend_->currentIndex() == 0 ? FftBackendKind::Fftw : FftBackendKind::Cuda;
  config.chirp_segmentation.window = static_cast<WindowFunction>(window_function_->currentIndex());
  config.chirp_segmentation.segment_fft_length = static_cast<std::uint32_t>(fft_length_->value());
  config.processing.dc_removal = dc_removal_->isChecked();
  config.processing.normalize = normalize_->isChecked();
  config.processing.peak_threshold_db = peak_threshold_->value();
  config.processing.peak_search_start_bin = static_cast<std::uint32_t>(peak_start_->value());
  config.processing.peak_search_end_bin = static_cast<std::uint32_t>(peak_end_->value());
  config.processing.peak_tracking_enabled = peak_tracking_->isChecked();
  config.processing.peak_tracking_max_delta_bins = static_cast<std::uint32_t>(peak_delta_->value());
  config.processing.peak_reacquire_width_bins = static_cast<std::uint32_t>(reacquire_width_->value());
  config.processing.peak_lost_policy = static_cast<PeakLostPolicy>(lost_policy_->currentIndex());
  config.chirp_segmentation.up_segment = {static_cast<std::uint32_t>(up_start_->value()),
                                         static_cast<std::uint32_t>(up_end_->value())};
  config.chirp_segmentation.down_segment = {static_cast<std::uint32_t>(down_start_->value()),
                                           static_cast<std::uint32_t>(down_end_->value())};
  config.chirp_segmentation.guard_samples = static_cast<std::uint32_t>(guard_samples_->value());
  config.storage.raw_enabled = raw_enabled_->isChecked();
  config.storage.processed_enabled = processed_enabled_->isChecked();
  config.storage.output_directory = output_directory_->text().trimmed().toStdString();
  config.storage.queue_capacity = static_cast<std::uint32_t>(storage_queue_->value());
  config.storage.split_file_size_gb = split_size_->value();
  config.udp.enabled = udp_enabled_->isChecked();
  config.udp.target_ip = udp_ip_->text().trimmed().toStdString();
  config.udp.target_port = static_cast<std::uint16_t>(udp_port_->value());
  config.udp.packet_point_count = static_cast<std::uint32_t>(udp_points_->value());
  return config;
}

void MainWindow::loadConfigToControls(const SystemConfig& config) {
  config_ = config;
  profile_combo_->clear();
  profile_combo_->addItem(QString::fromStdString(config.profile.name));
  digitizer_channel_->setCurrentIndex(config.digitizer.channel == DigitizerChannel::A ? 0 : 1);
  sample_rate_->setValue(config.digitizer.sample_rate_hz / 1.0e6);
  sample_point_->setValue(static_cast<int>(config.digitizer.sample_point));
  records_per_buffer_->setValue(static_cast<int>(config.digitizer.records_per_buffer));
  dma_buffer_count_->setValue(static_cast<int>(config.digitizer.dma_buffer_count));
  input_range_->setValue(config.digitizer.input_range_volts);
  coupling_->setCurrentIndex(config.digitizer.coupling == Coupling::Dc ? 0 : 1);
  trigger_level_->setValue(config.digitizer.trigger_level_percent);
  wavelength_->setValue(config.laser.wavelength_nm);
  sweep_bandwidth_->setValue(config.laser.sweep_bandwidth_hz / 1.0e9);
  chirp_period_->setValue(config.laser.chirp_period_us);
  laser_power_->setValue(config.laser.laser_power_mw);
  edfa_mode_->setCurrentIndex(static_cast<int>(config.edfa.mode));
  edfa_port_->setText(QString::fromStdString(config.edfa.port));
  edfa_control_mode_->setCurrentIndex(config.edfa.control_mode == EdfaControlMode::Apc ? 0
      : config.edfa.control_mode == EdfaControlMode::Acc ? 1 : 2);
  edfa_setpoint_->setValue(config.edfa.output_setpoint.value);
  edfa_warmup_->setValue(static_cast<int>(config.edfa.warmup_delay_ms));
  x_start_->setValue(config.scan.x_start_deg);
  x_end_->setValue(config.scan.x_end_deg);
  y_start_->setValue(config.scan.y_start_deg);
  y_end_->setValue(config.scan.y_end_deg);
  x_pixels_->setValue(static_cast<int>(config.scan.x_pixel_count));
  y_lines_->setValue(static_cast<int>(config.scan.y_line_count));
  line_time_->setValue(config.scan.line_time_ms);
  bidirectional_->setChecked(config.scan.bidirectional);
  mcu_enabled_->setChecked(config.mcu.enabled);
  mcu_port_->setText(QString::fromStdString(config.mcu.port));
  fft_backend_->setCurrentIndex(config.processing.fft_backend == FftBackendKind::Fftw ? 0 : 1);
  window_function_->setCurrentIndex(static_cast<int>(config.chirp_segmentation.window));
  fft_length_->setValue(static_cast<int>(config.chirp_segmentation.segment_fft_length));
  dc_removal_->setChecked(config.processing.dc_removal);
  normalize_->setChecked(config.processing.normalize);
  peak_threshold_->setValue(config.processing.peak_threshold_db);
  peak_start_->setValue(static_cast<int>(config.processing.peak_search_start_bin));
  peak_end_->setValue(static_cast<int>(config.processing.peak_search_end_bin));
  peak_tracking_->setChecked(config.processing.peak_tracking_enabled);
  peak_delta_->setValue(static_cast<int>(config.processing.peak_tracking_max_delta_bins));
  reacquire_width_->setValue(static_cast<int>(config.processing.peak_reacquire_width_bins));
  lost_policy_->setCurrentIndex(static_cast<int>(config.processing.peak_lost_policy));
  up_start_->setValue(static_cast<int>(config.chirp_segmentation.up_segment.start_sample));
  up_end_->setValue(static_cast<int>(config.chirp_segmentation.up_segment.end_sample_exclusive));
  down_start_->setValue(static_cast<int>(config.chirp_segmentation.down_segment.start_sample));
  down_end_->setValue(static_cast<int>(config.chirp_segmentation.down_segment.end_sample_exclusive));
  guard_samples_->setValue(static_cast<int>(config.chirp_segmentation.guard_samples));
  raw_enabled_->setChecked(config.storage.raw_enabled);
  processed_enabled_->setChecked(config.storage.processed_enabled);
  output_directory_->setText(QString::fromStdString(config.storage.output_directory));
  storage_queue_->setValue(static_cast<int>(config.storage.queue_capacity));
  split_size_->setValue(config.storage.split_file_size_gb);
  udp_enabled_->setChecked(config.udp.enabled);
  udp_ip_->setText(QString::fromStdString(config.udp.target_ip));
  udp_port_->setValue(config.udp.target_port);
  udp_points_->setValue(static_cast<int>(config.udp.packet_point_count));
  config_dirty_ = false;
  markDirty();
  config_dirty_ = false;
  validateControls();
}

void MainWindow::applyProfile() {
  if (!validateControls(true)) {
    return;
  }
  controller_->applyConfig(configFromControls());
}

void MainWindow::loadProfile() {
  const auto path = QFileDialog::getOpenFileName(this, "Load FMCW LiDAR profile", QString(), "YAML profile (*.yaml *.yml)");
  if (path.isEmpty()) {
    return;
  }
  const auto result = ConfigProfileCodec::loadLayered({fileSystemPath(path)});
  if (!result.ok()) {
    QString message;
    for (const auto& issue : result.issues) {
      message += QString("%1:%2 %3\n").arg(QString::fromStdString(issue.source)).arg(issue.line)
                     .arg(QString::fromStdString(issue.message));
    }
    QMessageBox::warning(this, "Profile load failed", message);
    return;
  }
  loadConfigToControls(result.config);
  appendLog("INFO", "Configuration", QString("Loaded profile %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::saveProfile() {
  if (!validateControls(true)) {
    return;
  }
  const auto path = QFileDialog::getSaveFileName(this, "Save FMCW LiDAR profile", "profile.yaml", "YAML profile (*.yaml)");
  if (path.isEmpty()) {
    return;
  }
  std::string error;
  const auto config = configFromControls();
  if (!ConfigProfileCodec::save(fileSystemPath(path), config, error)) {
    QMessageBox::critical(this, "Profile save failed", QString::fromStdString(error));
    return;
  }
  config_ = config;
  config_dirty_ = false;
  validateControls();
  appendLog("INFO", "Configuration", QString("Saved profile %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::updateStatus(RuntimeStatus status) {
  runtime_status_ = std::move(status);
  const auto state = QString::fromStdString(toString(runtime_status_.state)).toUpper();
  runtime_state_label_->setText(state);
  runtime_state_label_->setProperty("statusKind", runtime_status_.state == OperationState::Error ? "error"
      : runtime_status_.running ? "ready" : runtime_status_.connected ? "ready" : "neutral");
  repolish(runtime_state_label_);
  connect_button_->setText(runtime_status_.connected ? "Disconnect" : "Connect");
  start_stop_button_->setText(runtime_status_.running ? "STOP" : "START");
  start_stop_button_->setProperty("runState", runtime_status_.running ? "stop" : "start");
  repolish(start_stop_button_);
  apply_button_->setEnabled(!runtime_status_.running);
  load_button_->setEnabled(!runtime_status_.running);
  save_button_->setEnabled(!runtime_status_.running);

  overview_digitizer_->setText(runtime_status_.digitizer_ready ? "READY\nSingle channel" : "NOT READY");
  overview_edfa_->setText(runtime_status_.edfa_bypassed ? "BYPASS\nNo EDFA" :
                          runtime_status_.edfa_output_enabled ? "OUTPUT ON" : "READY\nOutput off");
  overview_mcu_->setText(runtime_status_.mcu_bypassed ? "BYPASS\nMCU disabled" :
                         runtime_status_.mcu_waveform_loaded ? "READY\nWaveform loaded" : "WAITING\nNo waveform");
  overview_processing_->setText(runtime_status_.backend_name.isEmpty() ? "NOT CONFIGURED" : runtime_status_.backend_name);
  overview_frames_->setText(QString("%1 received\n%2 processed")
                                .arg(runtime_status_.frames_received)
                                .arg(runtime_status_.frames_processed));
  overview_queues_->setText(QString("FFT %1 / %2\nWriter %3 / %4")
                                .arg(runtime_status_.processing_queue_size)
                                .arg(runtime_status_.processing_queue_capacity)
                                .arg(runtime_status_.storage_queue_size)
                                .arg(runtime_status_.storage_queue_capacity));
  overview_latency_->setText(QString("%1 ms\nRevision %2")
                                 .arg(runtime_status_.processing_latency_ms, 0, 'f', 3)
                                 .arg(runtime_status_.processing_revision));
  overview_recording_->setText(runtime_status_.recording
      ? QString("ACTIVE\n%1 frames").arg(runtime_status_.frames_written) : "OFF");
  overview_detail_->setText(QString("%1 | Config revision %2 | %3")
                                .arg(state)
                                .arg(runtime_status_.config_revision)
                                .arg(runtime_status_.detail));
  setStatusText(mcu_waveform_state_, runtime_status_.mcu_bypassed ? "MCU bypass active" :
                runtime_status_.mcu_waveform_loaded ? "Waveform loaded and ready" : "Upload required",
                runtime_status_.mcu_ready, runtime_status_.mcu_bypassed);
  edfa_output_button_->setText(runtime_status_.edfa_output_enabled ? "Disable Output" : "Enable Output");
  edfa_output_button_->setEnabled(runtime_status_.connected && edfa_mode_->currentIndex() == 2 &&
                                  !runtime_status_.edfa_bypassed && !runtime_status_.running);
  validateControls();
}

void MainWindow::setStatusText(QLabel* label, QString text, bool ready, bool bypassed) {
  label->setText(std::move(text));
  label->setProperty("statusKind", bypassed ? "neutral" : ready ? "ready" : "warn");
  repolish(label);
}

void MainWindow::appendLog(QString level, QString source, QString message) {
  const auto line = QString("%1 [%2] %3  %4")
      .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), level, source, message);
  log_entries_.append(line);
  while (log_entries_.size() > 2000) {
    log_entries_.removeFirst();
  }
  rebuildLog();
}

void MainWindow::rebuildLog() {
  const auto filter = log_filter_->currentText();
  QStringList visible;
  for (const auto& entry : log_entries_) {
    if (filter == "All levels" || entry.contains(QString("[%1]").arg(filter))) {
      visible.append(entry);
    }
  }
  log_view_->setPlainText(visible.join('\n'));
  log_view_->moveCursor(QTextCursor::End);
}

void MainWindow::saveCurrentView() {
  const auto path = QFileDialog::getSaveFileName(this, "Save current view", "fmcw_view.png", "PNG image (*.png)");
  if (path.isEmpty()) {
    return;
  }
  if (!live_tabs_->currentWidget()->grab().save(path, "PNG")) {
    QMessageBox::warning(this, "Save view", "The current view could not be written.");
  }
}

void MainWindow::startDemo() {
  if (validateControls()) {
    navigation_->setCurrentRow(1);
    controller_->startSystem(configFromControls());
  }
}

void MainWindow::captureSegmentation() {
  controller_->captureSegmentationSnapshot();
}

void MainWindow::showPage(int index) {
  navigation_->setCurrentRow(std::clamp(index, 0, navigation_->count() - 1));
}

void MainWindow::showLiveTab(int index) {
  navigation_->setCurrentRow(1);
  live_tabs_->setCurrentIndex(std::clamp(index, 0, live_tabs_->count() - 1));
}

}  // namespace fmcw
