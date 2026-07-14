#include "apps/common/main_window.h"

#include "core/app_version.h"
#include "core/config_profile.h"
#include "core/config_validation.h"
#include "core/digitizer_capabilities.h"

#include <QApplication>
#include <QAbstractSpinBox>
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
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
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
#include <cmath>
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

void tuneForm(QFormLayout* form) {
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setFormAlignment(Qt::AlignTop);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  form->setHorizontalSpacing(18);
  form->setVerticalSpacing(10);
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

QString sampleRateText(double sample_rate_hz) {
  if (sample_rate_hz >= 1.0e9) {
    return QString("%1 GS/s").arg(sample_rate_hz / 1.0e9, 0, 'g', 6);
  }
  if (sample_rate_hz >= 1.0e6) {
    return QString("%1 MS/s").arg(sample_rate_hz / 1.0e6, 0, 'g', 6);
  }
  return QString("%1 kS/s").arg(sample_rate_hz / 1.0e3, 0, 'g', 6);
}

QString darkStyleSheet() {
  return QStringLiteral(R"(
    QMainWindow, QWidget#workspace { background: #11171a; color: #dce5e7; }
    QWidget#sidebar { background: #0b1013; color: #edf4f3; }
    QLabel#brand { color: #f6f9f9; }
    QLabel#platform { color: #61c7ba; }
    QListWidget#navigation { color: #aebcc0; }
    QListWidget#navigation::item:hover { background: #182226; color: #ffffff; }
    QListWidget#navigation::item:selected { background: #203036; color: #ffffff; border-left: 3px solid #35b2a3; }
    QWidget#commandBar { background: #171f23; border-bottom: 1px solid #303b40; }
    QLabel#pageTitle { color: #f0f4f5; }
    QLabel[caption="true"] { color: #91a2a8; }
    QLabel[metric="true"] { color: #e8eff0; }
    QFrame[card="true"], QGroupBox[surface="true"] { background: #192226; border: 1px solid #303c41; }
    QGroupBox::title { color: #cbd6d9; }
    QPushButton { border-color: #3a494f; background: #202b30; color: #dce5e7; }
    QPushButton:hover { background: #29373c; border-color: #61757d; }
    QPushButton:pressed { background: #152024; }
    QPushButton:disabled { color: #68777c; background: #181f22; border-color: #2c363a; }
    QPushButton#applyButton { background: #173d3a; color: #7ee0d3; border-color: #347e76; }
    QPushButton#applyButton:hover { background: #1d4b47; border-color: #4da398; }
    QPushButton#connectButton { background: #31545c; color: #ffffff; border-color: #426b74; }
    QPushButton#startButton[runState="start"] { background: #19734f; color: #ffffff; border-color: #249169; }
    QPushButton#startButton[runState="stop"] { background: #bd5f29; color: #ffffff; border-color: #df7d43; }
    QPushButton#emergencyButton { background: #a82f38; color: #ffffff; border-color: #ca4c55; }
    QToolButton { border-color: #3a494f; background: #202b30; color: #dce5e7; }
    QToolButton:hover, QToolButton:checked { background: #21403e; border-color: #4b9f96; }
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { background: #12191d; color: #e1e8ea; border-color: #3a494f; selection-background-color: #277f76; }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #35b2a3; }
    QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background: #171e21; color: #708086; border-color: #2a3438; }
    QComboBox::drop-down { border: none; width: 24px; }
    QComboBox QAbstractItemView { background: #182125; color: #e1e8ea; border: 1px solid #425158; selection-background-color: #275f5a; }
    QCheckBox { color: #d6dfe1; spacing: 7px; }
    QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #53666d; border-radius: 2px; background: #12191d; }
    QCheckBox::indicator:checked { background: #2da899; border-color: #55cbbd; }
    QTabWidget::pane { border-color: #303c41; background: #192226; }
    QTabBar::tab { background: #141c20; color: #8fa0a6; border-color: #303c41; }
    QTabBar::tab:hover { background: #202b30; color: #dce5e7; }
    QTabBar::tab:selected { background: #192226; color: #62d0c2; border-top: 2px solid #35b2a3; }
    QLabel[statusKind="ready"], QPushButton#validationButton[statusKind="ready"] { background: #153a2b; color: #79d9ad; border-color: #286a50; }
    QLabel[statusKind="warn"], QPushButton#validationButton[statusKind="warn"] { background: #473619; color: #f2c873; border-color: #80632d; }
    QLabel[statusKind="error"], QPushButton#validationButton[statusKind="error"] { background: #452125; color: #f0959c; border-color: #7b3940; }
    QLabel[statusKind="neutral"], QPushButton#validationButton[statusKind="neutral"] { background: #263137; color: #afbdc1; border-color: #46555b; }
    QPlainTextEdit { background: #0c1114; color: #d5e0e2; border-color: #303c41; }
    QScrollArea, QScrollArea > QWidget > QWidget { background: #11171a; }
    QScrollBar:vertical { background: #11171a; width: 11px; margin: 0; }
    QScrollBar::handle:vertical { background: #405057; min-height: 28px; border-radius: 4px; }
    QScrollBar::handle:vertical:hover { background: #586b73; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QStatusBar { background: #151d21; color: #91a2a8; border-top-color: #303b40; }
    QToolTip { background: #263238; color: #f2f6f6; border: 1px solid #52646b; padding: 4px; }
  )");
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
    QLabel[statusKind="ready"], QPushButton#validationButton[statusKind="ready"] { background: #dff2e8; color: #12613f; border: 1px solid #a9d7bf; border-radius: 3px; padding: 3px 7px; font-weight: 600; }
    QLabel[statusKind="warn"], QPushButton#validationButton[statusKind="warn"] { background: #fff1d8; color: #8a5814; border: 1px solid #e6c37e; border-radius: 3px; padding: 3px 7px; font-weight: 600; }
    QLabel[statusKind="error"], QPushButton#validationButton[statusKind="error"] { background: #f9e2e4; color: #932c34; border: 1px solid #dda4a9; border-radius: 3px; padding: 3px 7px; font-weight: 600; }
    QLabel[statusKind="neutral"], QPushButton#validationButton[statusKind="neutral"] { background: #e8edef; color: #536168; border: 1px solid #ccd5d8; border-radius: 3px; padding: 3px 7px; font-weight: 600; }
    QPushButton#validationButton { min-height: 28px; padding: 0 9px; }
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
  apply_button_ = new QPushButton("Apply Setup", command_bar);
  apply_button_->setObjectName("applyButton");
  validation_button_ = new QPushButton("CHECKING", command_bar);
  validation_button_->setObjectName("validationButton");
  validation_button_->setProperty("statusKind", "neutral");
  validation_button_->setCursor(Qt::PointingHandCursor);
  validation_button_->setToolTip("Click to view validation details");
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
  command_layout->addWidget(validation_button_);
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

  for (auto* spin : findChildren<QAbstractSpinBox*>()) {
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignRight);
    spin->setKeyboardTracking(false);
  }

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

void MainWindow::setDarkTheme(bool enabled) {
  if (!enabled) {
    return;
  }

  QPalette palette = this->palette();
  palette.setColor(QPalette::Window, QColor("#11171a"));
  palette.setColor(QPalette::WindowText, QColor("#dce5e7"));
  palette.setColor(QPalette::Base, QColor("#151e22"));
  palette.setColor(QPalette::AlternateBase, QColor("#222e33"));
  palette.setColor(QPalette::Text, QColor("#dce5e7"));
  palette.setColor(QPalette::Button, QColor("#202b30"));
  palette.setColor(QPalette::ButtonText, QColor("#dce5e7"));
  palette.setColor(QPalette::Mid, QColor("#35444a"));
  palette.setColor(QPalette::Midlight, QColor("#64777e"));
  palette.setColor(QPalette::PlaceholderText, QColor("#82949b"));
  palette.setColor(QPalette::Highlight, QColor("#35b2a3"));
  palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
  qApp->setPalette(palette);
  setPalette(palette);
  setStyleSheet(styleSheet() + darkStyleSheet());
  update();
}

bool MainWindow::savePointCloudFramebuffer(const QString& path) {
  return point_cloud_plot_ != nullptr && point_cloud_plot_->grabFramebuffer().save(path, "PNG");
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
  auto* selected_label = new QLabel("Selected A-scan", content);
  selected_a_scan_ = new QSpinBox(content);
  selected_a_scan_->setRange(0, 0);
  selected_a_scan_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  selected_a_scan_->setAlignment(Qt::AlignRight);
  selected_a_scan_->setFixedWidth(72);
  selected_a_scan_->setToolTip("Record index displayed from each Alazar DMA buffer");
  selected_a_scan_status_ = new QLabel("Waiting for selected record", content);
  selected_a_scan_status_->setProperty("statusKind", "neutral");
  tools->addWidget(selected_label);
  tools->addWidget(selected_a_scan_);
  tools->addWidget(selected_a_scan_status_);
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
  connect(selected_a_scan_, &QSpinBox::valueChanged, this, [this](int value) {
    selected_a_scan_status_->setText(QString("Waiting for A-scan %1").arg(value));
    selected_a_scan_status_->setProperty("statusKind", "neutral");
    repolish(selected_a_scan_status_);
    controller_->setSelectedAScan(static_cast<std::uint32_t>(value));
  });
  tools->addWidget(auto_range);
  tools->addWidget(manual_range);
  tools->addWidget(freeze_button_);
  tools->addWidget(save_view);
  layout->addLayout(tools);

  live_tabs_ = new QTabWidget(content);
  time_plot_ = new LinePlotWidget(live_tabs_);
  time_plot_->setTitle("Full-period Time Domain");
  time_plot_->setAxisLabels("Sample", "ADC full scale");
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
  auto* point_cloud_page = new QWidget(live_tabs_);
  auto* point_cloud_layout = new QVBoxLayout(point_cloud_page);
  point_cloud_layout->setContentsMargins(0, 0, 0, 0);
  point_cloud_layout->setSpacing(8);
  auto* point_cloud_tools = new QHBoxLayout;
  auto* color_mode = new QComboBox(point_cloud_page);
  color_mode->addItems({"Intensity", "Velocity", "Distance"});
  color_mode->setToolTip("Point color source");
  auto* point_size = new QSlider(Qt::Horizontal, point_cloud_page);
  point_size->setRange(1, 10);
  point_size->setValue(3);
  point_size->setFixedWidth(110);
  point_size->setToolTip("Point size");
  auto* accumulate = new QCheckBox("Accumulate", point_cloud_page);
  auto* reset_camera = new QToolButton(point_cloud_page);
  reset_camera->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  reset_camera->setToolTip("Reset 3D camera");
  auto* save_cloud = new QToolButton(point_cloud_page);
  save_cloud->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_cloud->setToolTip("Save current point cloud as CSV");
  point_cloud_status_ = new QLabel("Waiting for point cloud", point_cloud_page);
  point_cloud_status_->setProperty("statusKind", "neutral");
  point_cloud_tools->addWidget(color_mode);
  point_cloud_tools->addWidget(new QLabel("Point size", point_cloud_page));
  point_cloud_tools->addWidget(point_size);
  point_cloud_tools->addWidget(accumulate);
  point_cloud_tools->addWidget(reset_camera);
  point_cloud_tools->addWidget(save_cloud);
  point_cloud_tools->addStretch(1);
  point_cloud_tools->addWidget(point_cloud_status_);
  point_cloud_plot_ = new PointCloudWidget(point_cloud_page);
  point_cloud_layout->addLayout(point_cloud_tools);
  point_cloud_layout->addWidget(point_cloud_plot_, 1);
  live_tabs_->addTab(time_plot_, "Time Domain");
  live_tabs_->addTab(fft_plot_, "FFT");
  live_tabs_->addTab(peak_page, "Peak Analysis");
  live_tabs_->addTab(distance_plot_, "Distance / Velocity");
  live_tabs_->addTab(bscan_plot_, "B-scan");
  live_tabs_->addTab(point_cloud_page, "3D Point Cloud");
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
    } else if (live_tabs_->currentIndex() == 4) {
      heatmap = bscan_plot_;
    } else {
      statusBar()->showMessage("3D view uses automatic spatial bounds", 3000);
      return;
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
  connect(color_mode, &QComboBox::currentIndexChanged, point_cloud_page, [this](int index) {
    point_cloud_plot_->setColorMode(index == 0 ? PointCloudColorMode::Intensity
        : index == 1 ? PointCloudColorMode::Velocity : PointCloudColorMode::Distance);
  });
  connect(point_size, &QSlider::valueChanged, point_cloud_page, [this](int value) {
    point_cloud_plot_->setPointSize(static_cast<float>(value));
  });
  connect(accumulate, &QCheckBox::toggled, point_cloud_plot_, &PointCloudWidget::setAccumulate);
  connect(reset_camera, &QToolButton::clicked, point_cloud_plot_, &PointCloudWidget::resetCamera);
  connect(save_cloud, &QToolButton::clicked, this, [this] {
    const auto path = QFileDialog::getSaveFileName(this, "Save point cloud", "point_cloud.csv",
                                                   "CSV point cloud (*.csv)");
    if (!path.isEmpty() && !point_cloud_plot_->saveCurrentCloud(path)) {
      QMessageBox::critical(this, "Point cloud save failed", "The current point cloud could not be written.");
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
  tuneForm(board_form);
  board_profile_ = new QComboBox(board);
  for (const auto& capability : digitizerBoardCapabilities()) {
    board_profile_->addItem(QString::fromStdString(capability.display_name),
                            QString::fromStdString(capability.profile_id));
  }
  board_address_ = new QLabel("System 1 / Board 1 | fixed", board);
  board_address_->setProperty("statusKind", "neutral");
  digitizer_channel_ = new QComboBox(board);
  digitizer_channel_->addItems({"Channel A", "Channel B"});
  sample_rate_ = new QComboBox(board);
  sample_point_ = new QSpinBox(board);
  sample_point_->setRange(256, 16 * 1024 * 1024);
  sample_point_->setSingleStep(static_cast<int>(kAts9371RecordResolution));
  input_range_ = new QComboBox(board);
  impedance_ = new QComboBox(board);
  coupling_ = new QComboBox(board);
  coupling_->addItem("DC");
  digitizer_lock_state_ = new QLabel("STOPPED | settings can be applied", board);
  digitizer_lock_state_->setProperty("statusKind", "neutral");
  board_form->addRow("Board model", board_profile_);
  board_form->addRow("Board address", board_address_);
  board_form->addRow("Input channel", digitizer_channel_);
  board_form->addRow("Sampling rate", sample_rate_);
  board_form->addRow("Sample points", sample_point_);
  board_form->addRow("Input range", input_range_);
  board_form->addRow("Impedance", impedance_);
  board_form->addRow("Coupling", coupling_);
  board_form->addRow("Setup state", digitizer_lock_state_);

  auto* dma = groupBox("DMA / Trigger", content);
  auto* dma_form = new QFormLayout(dma);
  tuneForm(dma_form);
  records_per_buffer_ = new QSpinBox(dma);
  records_per_buffer_->setRange(2, 15000);
  dma_buffer_count_ = new QSpinBox(dma);
  dma_buffer_count_->setRange(2, 128);
  auto* trigger_input = new QLabel("TRIG IN | External TTL | DC coupled", dma);
  trigger_input->setProperty("statusKind", "neutral");
  trigger_slope_ = new QComboBox(dma);
  trigger_slope_->addItems({"Rising edge", "Falling edge"});
  trigger_level_ = new QDoubleSpinBox(dma);
  trigger_level_->setRange(-100.0, 100.0);
  trigger_level_->setDecimals(1);
  trigger_level_->setSuffix(" % FS");
  trigger_level_code_ = new QLabel("SDK code 150", dma);
  trigger_level_code_->setProperty("statusKind", "neutral");
  trigger_delay_ = new QSpinBox(dma);
  trigger_delay_->setRange(0, 9999999);
  trigger_delay_->setSingleStep(static_cast<int>(kAts9371SingleChannelTriggerDelayAlignment));
  trigger_delay_->setSuffix(" samples");
  pre_trigger_ = new QSpinBox(dma);
  pre_trigger_->setRange(0, static_cast<int>(kAts9371MaxNptPretrigger));
  pre_trigger_->setSingleStep(static_cast<int>(kAts9371RecordResolution));
  pre_trigger_->setSuffix(" samples");
  post_trigger_ = new QLabel("4096 samples | derived", dma);
  post_trigger_->setProperty("statusKind", "neutral");
  auto* trigger_timeout = new QLabel("Wait forever | 0 ticks", dma);
  trigger_timeout->setProperty("statusKind", "neutral");
  auto* trigger_mode = new QLabel("One UP trigger produces one full-period record", dma);
  trigger_mode->setWordWrap(true);
  trigger_mode->setProperty("statusKind", "ready");
  dma_form->addRow("Records / buffer", records_per_buffer_);
  dma_form->addRow("DMA buffers", dma_buffer_count_);
  dma_form->addRow("Trigger input", trigger_input);
  dma_form->addRow("Trigger edge", trigger_slope_);
  dma_form->addRow("Trigger threshold", trigger_level_);
  dma_form->addRow("Threshold code", trigger_level_code_);
  dma_form->addRow("Trigger delay", trigger_delay_);
  dma_form->addRow("Pre-trigger", pre_trigger_);
  dma_form->addRow("Post-trigger", post_trigger_);
  dma_form->addRow("Trigger timeout", trigger_timeout);
  dma_form->addRow("Trigger contract", trigger_mode);
  layout->addWidget(board, 0, 0);
  layout->addWidget(dma, 0, 1);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  restart_required_controls_.append(QList<QWidget*>{board_profile_, digitizer_channel_, sample_rate_, sample_point_,
      input_range_, impedance_, coupling_, records_per_buffer_, dma_buffer_count_, trigger_slope_, trigger_level_,
      trigger_delay_, pre_trigger_});
  return wrapInScrollArea(content);
}

QWidget* MainWindow::buildLaserEdfaPage() {
  auto* content = new QWidget;
  auto* layout = new QGridLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto* laser = groupBox("Laser Specification", content);
  auto* laser_form = new QFormLayout(laser);
  tuneForm(laser_form);
  wavelength_ = new QDoubleSpinBox(laser);
  wavelength_->setRange(200.0, 3000.0);
  wavelength_->setDecimals(2);
  wavelength_->setSuffix(" nm");
  sweep_bandwidth_ = new QDoubleSpinBox(laser);
  sweep_bandwidth_->setRange(0.001, 1000.0);
  sweep_bandwidth_->setDecimals(3);
  sweep_bandwidth_->setSuffix(" GHz");
  sweep_rate_ = new QDoubleSpinBox(laser);
  sweep_rate_->setRange(0.001, 1000000.0);
  sweep_rate_->setDecimals(3);
  sweep_rate_->setSuffix(" THz/s");
  chirp_period_ = new QDoubleSpinBox(laser);
  chirp_period_->setRange(0.01, 100000.0);
  chirp_period_->setDecimals(3);
  chirp_period_->setSuffix(" us");
  laser_power_ = new QDoubleSpinBox(laser);
  laser_power_->setRange(0.0, 10000.0);
  laser_power_->setSuffix(" mW");
  laser_form->addRow("Wavelength", wavelength_);
  laser_form->addRow("Sweep bandwidth", sweep_bandwidth_);
  laser_form->addRow("Sweep rate", sweep_rate_);
  laser_form->addRow("Full chirp period", chirp_period_);
  laser_form->addRow("Laser power", laser_power_);

  auto* edfa = groupBox("Optional EDFA", content);
  auto* edfa_form = new QFormLayout(edfa);
  tuneForm(edfa_form);
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
  restart_required_controls_.append(QList<QWidget*>{wavelength_, sweep_bandwidth_, sweep_rate_, chirp_period_, laser_power_,
      edfa_mode_, edfa_port_, edfa_control_mode_, edfa_setpoint_, edfa_warmup_});
  return wrapInScrollArea(content);
}

QWidget* MainWindow::buildScanMcuPage() {
  auto* content = new QWidget;
  auto* layout = new QGridLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto* geometry = groupBox("Frame Layout", content);
  auto* geometry_form = new QFormLayout(geometry);
  tuneForm(geometry_form);
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
  a_scan_count_ = new QLabel("64 records | one DMA buffer", geometry);
  a_scan_count_->setProperty("statusKind", "neutral");
  y_lines_ = new QSpinBox(geometry);
  y_lines_->setRange(2, 10000);
  frame_point_count_ = new QLabel("1600 positions | derived", geometry);
  frame_point_count_->setProperty("statusKind", "neutral");
  dma_bscan_rate_ = new QLabel("Waiting for DMA | measured at runtime", geometry);
  dma_bscan_rate_->setProperty("statusKind", "neutral");
  frame_time_ = new QLabel("Waiting for DMA | measured at runtime", geometry);
  frame_time_->setProperty("statusKind", "neutral");
  bidirectional_ = new QCheckBox("Bidirectional raster", geometry);
  geometry_form->addRow("X start", x_start_);
  geometry_form->addRow("X end", x_end_);
  geometry_form->addRow("Y start", y_start_);
  geometry_form->addRow("Y end", y_end_);
  geometry_form->addRow("A-scans / B-scan", a_scan_count_);
  geometry_form->addRow("B-scans / frame", y_lines_);
  geometry_form->addRow("Positions / frame", frame_point_count_);
  geometry_form->addRow("DMA B-scan rate", dma_bscan_rate_);
  geometry_form->addRow("Measured frame time", frame_time_);
  geometry_form->addRow("Scan direction", bidirectional_);

  auto* mcu = groupBox("MCU Waveform", content);
  auto* mcu_form = new QFormLayout(mcu);
  tuneForm(mcu_form);
  mcu_enabled_ = new QCheckBox("Use MCU scan and trigger controller", mcu);
  mcu_port_ = new QLineEdit(mcu);
  mcu_port_->setPlaceholderText(platform_name_ == "Windows" ? "COM4" : "/dev/ttyACM0");
  mcu_point_rate_ = new QLabel("100 kHz | firmware TIM6", mcu);
  mcu_point_rate_->setProperty("statusKind", "neutral");
  mcu_frame_time_ = new QLabel("16.000 ms | 1600 points / frame", mcu);
  mcu_frame_time_->setProperty("statusKind", "neutral");
  frame_sync_state_ = new QLabel("Waiting for DMA timing", mcu);
  frame_sync_state_->setProperty("statusKind", "neutral");
  upload_waveform_button_ = new QPushButton("Upload Waveform", mcu);
  mcu_waveform_state_ = new QLabel("MCU bypass active", mcu);
  mcu_waveform_state_->setProperty("statusKind", "neutral");
  mcu_form->addRow("Controller", mcu_enabled_);
  mcu_form->addRow("Serial port", mcu_port_);
  mcu_form->addRow("Point rate", mcu_point_rate_);
  mcu_form->addRow("Cycle / frame", mcu_frame_time_);
  mcu_form->addRow("DMA / MCU sync", frame_sync_state_);
  mcu_form->addRow("Full-frame waveform", upload_waveform_button_);
  mcu_form->addRow("Readiness", mcu_waveform_state_);
  layout->addWidget(geometry, 0, 0);
  layout->addWidget(mcu, 0, 1);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  restart_required_controls_.append(QList<QWidget*>{x_start_, x_end_, y_start_, y_end_, y_lines_, bidirectional_,
      mcu_enabled_, mcu_port_, upload_waveform_button_});
  return wrapInScrollArea(content);
}

QWidget* MainWindow::buildProcessingPage() {
  auto* content = new QWidget;
  auto* layout = new QGridLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  auto* fft = groupBox("FFT / Preprocessing", content);
  auto* fft_form = new QFormLayout(fft);
  tuneForm(fft_form);
  fft_backend_ = new QComboBox(fft);
  fft_backend_->addItems({"FFTW (CPU)", "CUDA cuFFT"});
  window_function_ = new QComboBox(fft);
  window_function_->addItems({"Hann", "Hamming", "Blackman", "Rectangular"});
  dc_removal_ = new QCheckBox("Remove DC component", fft);
  fft_length_ = new QSpinBox(fft);
  fft_length_->setRange(256, 1048576);
  fft_length_->setSingleStep(256);
  fft_form->addRow("Backend", fft_backend_);
  fft_form->addRow("Window", window_function_);
  fft_form->addRow("FFT length", fft_length_);
  fft_form->addRow("DC removal", dc_removal_);

  auto* peak = groupBox("Peak Detection", content);
  auto* peak_form = new QFormLayout(peak);
  tuneForm(peak_form);
  peak_threshold_ = new QDoubleSpinBox(peak);
  peak_threshold_->setRange(-180.0, 20.0);
  peak_threshold_->setDecimals(1);
  peak_threshold_->setSuffix(" dB");
  peak_start_ = new QSpinBox(peak);
  peak_start_->setRange(0, 1048576);
  peak_end_ = new QSpinBox(peak);
  peak_end_->setRange(1, 1048576);
  auto* detection_mode = new QLabel("Highest peak above threshold per A-scan", peak);
  detection_mode->setWordWrap(true);
  detection_mode->setProperty("statusKind", "ready");
  auto* update_runtime = new QPushButton("Apply Processing", peak);
  peak_form->addRow("Peak threshold", peak_threshold_);
  peak_form->addRow("Search start bin", peak_start_);
  peak_form->addRow("Search end bin", peak_end_);
  peak_form->addRow("Method", detection_mode);
  peak_form->addRow("Runtime update", update_runtime);

  auto* segmentation = groupBox("Chirp Segmentation Snapshot", content);
  auto* segmentation_layout = new QVBoxLayout(segmentation);
  segmentation_layout->setSpacing(8);
  auto* controls = new QGridLayout;
  controls->setHorizontalSpacing(8);
  controls->setVerticalSpacing(4);
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
  int control_column = 0;
  auto addControl = [&controls, &control_column](const QString& label, QWidget* widget) {
    auto* caption = new QLabel(label);
    caption->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    caption->setProperty("caption", true);
    controls->addWidget(caption, 0, control_column);
    controls->addWidget(widget, 1, control_column);
    controls->setColumnMinimumWidth(control_column, 112);
    ++control_column;
  };
  addControl("UP start", up_start_);
  addControl("UP end", up_end_);
  addControl("DOWN start", down_start_);
  addControl("DOWN end", down_end_);
  addControl("Guard", guard_samples_);
  controls->setColumnStretch(control_column, 1);
  auto* capture = new QPushButton("Capture Snapshot", segmentation);
  controls->addWidget(capture, 1, control_column + 1);
  segmentation_state_ = new QLabel("No frozen frame", segmentation);
  segmentation_state_->setProperty("statusKind", "neutral");
  segmentation_plot_ = new SegmentationPlotWidget(segmentation);
  segmentation_layout->addLayout(controls);
  segmentation_layout->addWidget(segmentation_state_);
  segmentation_layout->addWidget(segmentation_plot_, 1);
  restart_required_controls_.append(QList<QWidget*>{fft_backend_, window_function_, fft_length_});
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
  tuneForm(storage_form);
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
  tuneForm(udp_form);
  udp_enabled_ = new QCheckBox("Enable UDP point output", udp);
  udp_ip_ = new QLineEdit(udp);
  udp_port_ = new QSpinBox(udp);
  udp_port_->setRange(1, 65535);
  udp_points_ = new QSpinBox(udp);
  udp_points_->setRange(1, 3273);
  udp_version_ = new QComboBox(udp);
  udp_version_->addItem("v1 | little endian", 1);
  udp_queue_ = new QSpinBox(udp);
  udp_queue_->setRange(1, 65536);
  udp_policy_ = new QComboBox(udp);
  udp_policy_->addItems({"Latest frame", "Preserve queued frames", "Stop sending"});
  udp_status_ = new QLabel("UDP off", udp);
  udp_status_->setWordWrap(true);
  udp_status_->setProperty("statusKind", "neutral");
  udp_form->addRow("Output", udp_enabled_);
  udp_form->addRow("Target IPv4", udp_ip_);
  udp_form->addRow("Target port", udp_port_);
  udp_form->addRow("Points / packet", udp_points_);
  udp_form->addRow("Packet format", udp_version_);
  udp_form->addRow("Sender queue", udp_queue_);
  udp_form->addRow("On queue full", udp_policy_);
  udp_form->addRow("Runtime", udp_status_);
  layout->addWidget(storage, 0, 0);
  layout->addWidget(udp, 0, 1);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  restart_required_controls_.append(QList<QWidget*>{raw_enabled_, processed_enabled_, output_directory_, browse,
      storage_queue_, split_size_, udp_enabled_, udp_ip_, udp_port_, udp_points_, udp_version_, udp_queue_,
      udp_policy_});
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
  connect(validation_button_, &QPushButton::clicked, this, &MainWindow::showValidationDetails);
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
            if (command == "Processing update") {
              config_.processing = configFromControls().processing;
              config_dirty_ = restart_dirty_;
              validateControls();
            } else if (command == "Apply configuration" || command == "Connect" || command == "Start") {
              config_ = configFromControls();
              config_dirty_ = false;
              restart_dirty_ = false;
              if (!runtime_status_.running) {
                digitizer_lock_state_->setText("READY | board settings applied");
                digitizer_lock_state_->setProperty("statusKind", "ready");
                repolish(digitizer_lock_state_);
              }
              validateControls();
            }
          });
  connect(controller_, &ApplicationController::waveformReady, this, [this](WaveformSnapshotPtr snapshot) {
    if (freeze_live_ || snapshot == nullptr) {
      return;
    }
    time_plot_->setSeries({{"Full period", qVector(snapshot->full_scale_samples), QColor("#167a86")}});
    selected_a_scan_status_->setText(QString("A-scan %1 / %2 | DMA %3")
                                         .arg(snapshot->record_index_in_buffer)
                                         .arg(snapshot->records_in_buffer - 1U)
                                         .arg(snapshot->dma_buffer_sequence));
    selected_a_scan_status_->setProperty("statusKind", "ready");
    repolish(selected_a_scan_status_);
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
  connect(controller_, &ApplicationController::pointCloudReady, this, [this](PointCloudSnapshotPtr snapshot) {
    if (freeze_live_ || snapshot == nullptr) {
      return;
    }
    const auto update_interval_ms = static_cast<qint64>(
        1000.0 / std::clamp(config_.ui.point_cloud_update_hz, 1.0, 60.0));
    if (point_cloud_update_timer_.isValid() && point_cloud_update_timer_.elapsed() < update_interval_ms) {
      return;
    }
    if (point_cloud_update_timer_.isValid()) {
      point_cloud_update_timer_.restart();
    } else {
      point_cloud_update_timer_.start();
    }
    point_cloud_plot_->setSnapshot(snapshot);
    const auto valid_points = std::count_if(snapshot->points.begin(), snapshot->points.end(),
                                            [](const PointXYZI& point) { return point.valid; });
    point_cloud_status_->setText(QString("Frame %1 | %2 / %3 lines | %4 points")
                                     .arg(snapshot->scan_frame_index + 1U)
                                     .arg(snapshot->completed_lines)
                                     .arg(snapshot->height)
                                     .arg(valid_points));
    point_cloud_status_->setProperty("statusKind", snapshot->complete ? "ready" : "neutral");
    repolish(point_cloud_status_);
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
      board_profile_, digitizer_channel_, sample_rate_, sample_point_, records_per_buffer_, dma_buffer_count_,
      input_range_, impedance_, coupling_, trigger_slope_, trigger_level_, trigger_delay_, pre_trigger_,
      wavelength_, sweep_bandwidth_, sweep_rate_, chirp_period_, laser_power_, edfa_mode_, edfa_port_,
      edfa_control_mode_, edfa_setpoint_, edfa_warmup_, x_start_, x_end_, y_start_, y_end_, y_lines_,
      bidirectional_, mcu_enabled_, mcu_port_, fft_backend_, window_function_, dc_removal_,
      peak_threshold_, peak_start_, peak_end_,
      up_start_, up_end_, down_start_, down_end_, guard_samples_, fft_length_, raw_enabled_, processed_enabled_,
      output_directory_, storage_queue_, split_size_, udp_enabled_, udp_ip_, udp_port_, udp_points_, udp_version_,
      udp_queue_, udp_policy_};
  const QList<QObject*> runtime_controls = {dc_removal_, peak_threshold_, peak_start_, peak_end_};
  connect(board_profile_, &QComboBox::currentIndexChanged, this, [this] {
    populateDigitizerCapabilities(board_profile_->currentData().toString(), 0.0, 0.0, 0U);
  });
  connect(records_per_buffer_, &QSpinBox::valueChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  connect(y_lines_, &QSpinBox::valueChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  connect(sample_point_, &QSpinBox::valueChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  connect(pre_trigger_, &QSpinBox::valueChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  connect(trigger_level_, &QDoubleSpinBox::valueChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  for (auto* control : config_controls) {
    const auto changed = [this, restart_required = !runtime_controls.contains(control)] {
      if (restart_required) {
        markRestartDirty();
      } else {
        markDirty();
      }
    };
    if (auto* spin = qobject_cast<QSpinBox*>(control)) {
      connect(spin, &QSpinBox::valueChanged, this, changed);
    } else if (auto* double_spin = qobject_cast<QDoubleSpinBox*>(control)) {
      connect(double_spin, &QDoubleSpinBox::valueChanged, this, changed);
    } else if (auto* combo = qobject_cast<QComboBox*>(control)) {
      connect(combo, &QComboBox::currentIndexChanged, this, changed);
    } else if (auto* check = qobject_cast<QCheckBox*>(control)) {
      connect(check, &QCheckBox::toggled, this, changed);
    } else if (auto* edit = qobject_cast<QLineEdit*>(control)) {
      connect(edit, &QLineEdit::textChanged, this, changed);
    }
  }
}

void MainWindow::markDirty() {
  if (loading_controls_) {
    return;
  }
  config_dirty_ = true;
  raw_indicator_->setText(raw_enabled_->isChecked() ? "RAW ON" : "RAW OFF");
  raw_indicator_->setProperty("statusKind", raw_enabled_->isChecked() ? "ready" : "neutral");
  udp_indicator_->setText(udp_enabled_->isChecked() ? "UDP READY" : "UDP OFF");
  udp_indicator_->setProperty("statusKind", udp_enabled_->isChecked() ? "ready" : "neutral");
  repolish(raw_indicator_);
  repolish(udp_indicator_);
  if (restart_dirty_ && !runtime_status_.running && digitizer_lock_state_ != nullptr) {
    digitizer_lock_state_->setText("APPLY REQUIRED | reconnect before START");
    digitizer_lock_state_->setProperty("statusKind", "warn");
    repolish(digitizer_lock_state_);
  }
  segmentation_plot_->setSegments({static_cast<std::uint32_t>(up_start_->value()),
                                    static_cast<std::uint32_t>(up_end_->value())},
                                   {static_cast<std::uint32_t>(down_start_->value()),
                                    static_cast<std::uint32_t>(down_end_->value())},
                                   static_cast<std::uint32_t>(guard_samples_->value()));
  validateControls();
}

void MainWindow::markRestartDirty() {
  if (loading_controls_) {
    return;
  }
  restart_dirty_ = true;
  markDirty();
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
    validation_button_->setText(QString("%1 ERROR").arg(errors));
    validation_button_->setIcon(style()->standardIcon(QStyle::SP_MessageBoxCritical));
    validation_button_->setProperty("statusKind", "error");
  } else if (warnings > 0) {
    validation_button_->setText(config_dirty_ ? QString("VALID | %1 WARN | APPLY").arg(warnings)
                                              : QString("VALID | %1 WARN").arg(warnings));
    validation_button_->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    validation_button_->setProperty("statusKind", "warn");
  } else {
    validation_button_->setText(config_dirty_ ? "VALID | APPLY" : "VALID");
    validation_button_->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    validation_button_->setProperty("statusKind", "ready");
  }
  validation_button_->setToolTip(tooltip.isEmpty()
      ? "Configuration is valid. Click to view validation details."
      : QString("Click to view validation details.\n\n%1").arg(tooltip));
  repolish(validation_button_);
  start_stop_button_->setEnabled(runtime_status_.running || (errors == 0 && !config_dirty_));
  apply_button_->setEnabled(!runtime_status_.running && errors == 0);
  if (show_dialog && errors > 0) {
    QMessageBox::warning(this, "Configuration validation", tooltip);
  }
  return errors == 0;
}

void MainWindow::showValidationDetails() {
  const auto result = ConfigValidator::validate(configFromControls());
  int errors = 0;
  int warnings = 0;
  QString details;

  for (const auto& issue : result.issues) {
    if (issue.severity == ValidationSeverity::Error) {
      ++errors;
    } else if (issue.severity == ValidationSeverity::Warning) {
      ++warnings;
    }

    const auto severity = QString::fromStdString(toString(issue.severity)).toUpper().toHtmlEscaped();
    const auto path = QString::fromStdString(issue.path).toHtmlEscaped();
    const auto message = QString::fromStdString(issue.message).toHtmlEscaped();
    const auto action = QString::fromStdString(issue.action).toHtmlEscaped();
    details += QString("<p><b>%1</b><br>Field: <code>%2</code><br>Issue: %3<br>Action: %4</p>")
                   .arg(severity, path, message, action);
  }

  QMessageBox dialog(this);
  dialog.setWindowTitle("Setup validation");
  dialog.setTextFormat(Qt::RichText);
  dialog.setStandardButtons(QMessageBox::Ok);
  dialog.setStyleSheet("QLabel#qt_msgbox_informativelabel { min-width: 620px; }");

  if (errors > 0) {
    dialog.setIcon(QMessageBox::Critical);
    dialog.setText(QString("<b>%1 configuration error(s) block Apply, Connect, and START.</b>").arg(errors));
  } else if (warnings > 0) {
    dialog.setIcon(QMessageBox::Warning);
    dialog.setText(QString("<b>Configuration is valid with %1 warning(s).</b><br>Warnings do not block START, but should be reviewed before operation.").arg(warnings));
  } else {
    dialog.setIcon(QMessageBox::Information);
    dialog.setText("<b>Configuration is valid.</b>");
    details = "No warnings or errors were found.";
  }

  dialog.setInformativeText(details);
  dialog.exec();
}

SystemConfig MainWindow::configFromControls() const {
  auto config = config_;
  config.profile.name = profile_combo_->currentText().toStdString();
  config.digitizer.board_profile = board_profile_->currentData().toString().toStdString();
  config.digitizer.system_id = kAlazarSystemId;
  config.digitizer.board_id = kAlazarBoardId;
  config.digitizer.channel = digitizer_channel_->currentIndex() == 0 ? DigitizerChannel::A : DigitizerChannel::B;
  config.digitizer.sample_rate_hz = sample_rate_->currentData().toDouble();
  config.digitizer.sample_point = static_cast<std::uint32_t>(sample_point_->value());
  config.digitizer.records_per_buffer = static_cast<std::uint32_t>(records_per_buffer_->value());
  config.digitizer.dma_buffer_count = static_cast<std::uint32_t>(dma_buffer_count_->value());
  config.digitizer.input_range_volts = input_range_->currentData().toDouble();
  config.digitizer.impedance_ohms = impedance_->currentData().toUInt();
  config.digitizer.coupling = Coupling::Dc;
  config.digitizer.trigger_source = TriggerSource::External;
  config.digitizer.trigger_slope = trigger_slope_->currentIndex() == 0 ? TriggerSlope::Rising : TriggerSlope::Falling;
  config.digitizer.trigger_level_percent = trigger_level_->value();
  config.digitizer.trigger_delay_samples = static_cast<std::uint32_t>(trigger_delay_->value());
  config.digitizer.pre_trigger_samples = static_cast<std::uint32_t>(pre_trigger_->value());
  config.digitizer.post_trigger_samples = config.digitizer.sample_point > config.digitizer.pre_trigger_samples
      ? config.digitizer.sample_point - config.digitizer.pre_trigger_samples
      : 0U;
  config.laser.wavelength_nm = wavelength_->value();
  config.laser.sweep_bandwidth_hz = sweep_bandwidth_->value() * 1.0e9;
  config.laser.sweep_rate_hz_per_s = sweep_rate_->value() * 1.0e12;
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
  config.scan.y_line_count = static_cast<std::uint32_t>(y_lines_->value());
  config.scan.bidirectional = bidirectional_->isChecked();
  config.scan.x_pixel_count = derivedAScanCount(config);
  config.digitizer.a_scan_count = derivedAScanCount(config);
  config.digitizer.b_scan_count = config.scan.y_line_count;
  config.mcu.enabled = mcu_enabled_->isChecked();
  config.mcu.port = mcu_port_->text().trimmed().toStdString();
  config.processing.fft_backend = fft_backend_->currentIndex() == 0 ? FftBackendKind::Fftw : FftBackendKind::Cuda;
  config.chirp_segmentation.window = static_cast<WindowFunction>(window_function_->currentIndex());
  config.chirp_segmentation.segment_fft_length = static_cast<std::uint32_t>(fft_length_->value());
  config.processing.dc_removal = dc_removal_->isChecked();
  config.processing.peak_threshold_db = peak_threshold_->value();
  config.processing.peak_search_start_bin = static_cast<std::uint32_t>(peak_start_->value());
  config.processing.peak_search_end_bin = static_cast<std::uint32_t>(peak_end_->value());
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
  config.udp.packet_format_version = udp_version_->currentData().toUInt();
  config.udp.queue_capacity = static_cast<std::uint32_t>(udp_queue_->value());
  config.udp.backpressure_policy = udp_policy_->currentIndex() == 0 ? UdpBackpressurePolicy::LatestFrame
      : udp_policy_->currentIndex() == 1 ? UdpBackpressurePolicy::PreserveFrames
                                         : UdpBackpressurePolicy::StopSending;
  return config;
}

void MainWindow::populateDigitizerCapabilities(QString profile_id, double preferred_rate_hz,
                                                double preferred_range_volts,
                                                std::uint32_t preferred_impedance) {
  const QSignalBlocker rate_blocker(sample_rate_);
  const QSignalBlocker range_blocker(input_range_);
  const QSignalBlocker impedance_blocker(impedance_);
  const auto* capabilities = findDigitizerBoardCapabilities(profile_id.toStdString());
  if (capabilities == nullptr && !digitizerBoardCapabilities().empty()) {
    capabilities = &digitizerBoardCapabilities().front();
  }
  sample_rate_->clear();
  input_range_->clear();
  impedance_->clear();
  if (capabilities == nullptr) {
    return;
  }
  int preferred_rate_index = 0;
  for (std::size_t index = 0; index < capabilities->sample_rates_hz.size(); ++index) {
    const auto rate = capabilities->sample_rates_hz[index];
    sample_rate_->addItem(sampleRateText(rate), rate);
    if (std::abs(rate - preferred_rate_hz) <= 1.0) {
      preferred_rate_index = static_cast<int>(index);
    }
  }
  int preferred_range_index = 0;
  for (std::size_t index = 0; index < capabilities->input_ranges_volts.size(); ++index) {
    const auto range = capabilities->input_ranges_volts[index];
    input_range_->addItem(QString("+/- %1 mV").arg(range * 1000.0, 0, 'g', 8), range);
    if (std::abs(range - preferred_range_volts) <= 1.0e-9) {
      preferred_range_index = static_cast<int>(index);
    }
  }
  int preferred_impedance_index = 0;
  for (std::size_t index = 0; index < capabilities->impedances_ohms.size(); ++index) {
    const auto impedance = capabilities->impedances_ohms[index];
    impedance_->addItem(QString("%1 ohm").arg(impedance), impedance);
    if (impedance == preferred_impedance) {
      preferred_impedance_index = static_cast<int>(index);
    }
  }
  sample_rate_->setCurrentIndex(preferred_rate_index);
  input_range_->setCurrentIndex(preferred_range_index);
  impedance_->setCurrentIndex(preferred_impedance_index);
}

void MainWindow::updateDerivedAcquisitionLabels() {
  if (sample_point_ == nullptr || pre_trigger_ == nullptr) {
    return;
  }
  const QSignalBlocker pre_trigger_blocker(pre_trigger_);
  pre_trigger_->setMaximum(std::min(sample_point_->value(), static_cast<int>(kAts9371MaxNptPretrigger)));
  if (pre_trigger_->value() > sample_point_->value()) {
    pre_trigger_->setValue(sample_point_->value());
  }
  const auto post_trigger = sample_point_->value() - pre_trigger_->value();
  post_trigger_->setText(QString("%1 samples | derived").arg(post_trigger));
  trigger_level_code_->setText(QString("SDK code %1").arg(alazarTriggerLevelCode(trigger_level_->value())));
  const auto a_scans = records_per_buffer_->value();
  if (selected_a_scan_ != nullptr) {
    selected_a_scan_->setMaximum(std::max(0, a_scans - 1));
  }
  const auto b_scans = y_lines_->value();
  const auto frame_points = static_cast<qulonglong>(a_scans) * static_cast<qulonglong>(b_scans);
  a_scan_count_->setText(QString("%1 records | one DMA buffer").arg(a_scans));
  frame_point_count_->setText(QString("%1 positions | %2 x %3")
                                  .arg(frame_points)
                                  .arg(a_scans)
                                  .arg(b_scans));
  if (runtime_status_.dma_bscan_rate_hz > 0.0 && runtime_status_.dma_bscan_period_ms > 0.0) {
    dma_bscan_rate_->setText(QString("%1 B-scans/s | %2 ms/buffer")
                                 .arg(runtime_status_.dma_bscan_rate_hz, 0, 'f', 2)
                                 .arg(runtime_status_.dma_bscan_period_ms, 0, 'f', 3));
    frame_time_->setText(QString("%1 ms | %2 measured DMA buffers")
                             .arg(runtime_status_.dma_bscan_period_ms * b_scans, 0, 'f', 3)
                             .arg(b_scans));
  } else {
    dma_bscan_rate_->setText("Waiting for DMA | measured at runtime");
    frame_time_->setText("Waiting for DMA | measured at runtime");
  }
  const auto point_rate_hz = config_.scan.scanner_sample_rate_hz;
  mcu_point_rate_->setText(QString("%1 kHz | firmware TIM6").arg(point_rate_hz / 1000.0, 0, 'g', 6));
  const auto mcu_frame_time_ms = point_rate_hz > 0.0
      ? static_cast<double>(frame_points) * 1000.0 / point_rate_hz
      : 0.0;
  mcu_frame_time_->setText(QString("%1 ms | %2 points / frame")
                               .arg(mcu_frame_time_ms, 0, 'f', 3)
                               .arg(frame_points));
  if (runtime_status_.dma_bscan_period_ms > 0.0 && mcu_frame_time_ms > 0.0) {
    const auto dma_frame_time_ms = runtime_status_.dma_bscan_period_ms * b_scans;
    const auto difference_percent = std::abs(dma_frame_time_ms - mcu_frame_time_ms) /
        dma_frame_time_ms * 100.0;
    const auto synchronized = difference_percent <= 5.0;
    frame_sync_state_->setText(synchronized
        ? QString("SYNC | difference %1 %").arg(difference_percent, 0, 'f', 2)
        : QString("MISMATCH | DMA %1 ms vs MCU %2 ms")
              .arg(dma_frame_time_ms, 0, 'f', 3)
              .arg(mcu_frame_time_ms, 0, 'f', 3));
    frame_sync_state_->setProperty("statusKind", synchronized ? "ready" : "warn");
  } else {
    frame_sync_state_->setText("Waiting for DMA timing");
    frame_sync_state_->setProperty("statusKind", "neutral");
  }
  repolish(frame_sync_state_);
}

void MainWindow::loadConfigToControls(const SystemConfig& config, bool mark_pending) {
  loading_controls_ = true;
  config_ = config;
  profile_combo_->clear();
  profile_combo_->addItem(QString::fromStdString(config.profile.name));
  const auto profile_index = board_profile_->findData(QString::fromStdString(config.digitizer.board_profile));
  board_profile_->setCurrentIndex(profile_index >= 0 ? profile_index : 0);
  populateDigitizerCapabilities(board_profile_->currentData().toString(), config.digitizer.sample_rate_hz,
                                config.digitizer.input_range_volts, config.digitizer.impedance_ohms);
  digitizer_channel_->setCurrentIndex(config.digitizer.channel == DigitizerChannel::A ? 0 : 1);
  sample_point_->setValue(static_cast<int>(config.digitizer.sample_point));
  records_per_buffer_->setValue(static_cast<int>(config.digitizer.records_per_buffer));
  dma_buffer_count_->setValue(static_cast<int>(config.digitizer.dma_buffer_count));
  coupling_->setCurrentIndex(0);
  trigger_slope_->setCurrentIndex(config.digitizer.trigger_slope == TriggerSlope::Rising ? 0 : 1);
  trigger_level_->setValue(config.digitizer.trigger_level_percent);
  trigger_delay_->setValue(static_cast<int>(config.digitizer.trigger_delay_samples));
  pre_trigger_->setValue(static_cast<int>(config.digitizer.pre_trigger_samples));
  wavelength_->setValue(config.laser.wavelength_nm);
  sweep_bandwidth_->setValue(config.laser.sweep_bandwidth_hz / 1.0e9);
  sweep_rate_->setValue(config.laser.sweep_rate_hz_per_s / 1.0e12);
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
  y_lines_->setValue(static_cast<int>(config.scan.y_line_count));
  bidirectional_->setChecked(config.scan.bidirectional);
  mcu_enabled_->setChecked(config.mcu.enabled);
  mcu_port_->setText(QString::fromStdString(config.mcu.port));
  fft_backend_->setCurrentIndex(config.processing.fft_backend == FftBackendKind::Fftw ? 0 : 1);
  window_function_->setCurrentIndex(static_cast<int>(config.chirp_segmentation.window));
  fft_length_->setValue(static_cast<int>(config.chirp_segmentation.segment_fft_length));
  dc_removal_->setChecked(config.processing.dc_removal);
  peak_threshold_->setValue(config.processing.peak_threshold_db);
  peak_start_->setValue(static_cast<int>(config.processing.peak_search_start_bin));
  peak_end_->setValue(static_cast<int>(config.processing.peak_search_end_bin));
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
  udp_version_->setCurrentIndex(std::max(0, udp_version_->findData(config.udp.packet_format_version)));
  udp_queue_->setValue(static_cast<int>(config.udp.queue_capacity));
  udp_policy_->setCurrentIndex(config.udp.backpressure_policy == UdpBackpressurePolicy::LatestFrame ? 0
      : config.udp.backpressure_policy == UdpBackpressurePolicy::PreserveFrames ? 1 : 2);
  updateDerivedAcquisitionLabels();
  loading_controls_ = false;
  config_dirty_ = mark_pending;
  restart_dirty_ = mark_pending;
  raw_indicator_->setText(raw_enabled_->isChecked() ? "RAW ON" : "RAW OFF");
  raw_indicator_->setProperty("statusKind", raw_enabled_->isChecked() ? "ready" : "neutral");
  udp_indicator_->setText(udp_enabled_->isChecked() ? "UDP READY" : "UDP OFF");
  udp_indicator_->setProperty("statusKind", udp_enabled_->isChecked() ? "ready" : "neutral");
  repolish(raw_indicator_);
  repolish(udp_indicator_);
  segmentation_plot_->setSegments({static_cast<std::uint32_t>(up_start_->value()),
                                    static_cast<std::uint32_t>(up_end_->value())},
                                   {static_cast<std::uint32_t>(down_start_->value()),
                                    static_cast<std::uint32_t>(down_end_->value())},
                                   static_cast<std::uint32_t>(guard_samples_->value()));
  digitizer_lock_state_->setText(mark_pending ? "APPLY REQUIRED | reconnect before START"
                                              : "READY | board settings applied");
  digitizer_lock_state_->setProperty("statusKind", mark_pending ? "warn" : "ready");
  repolish(digitizer_lock_state_);
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
  loadConfigToControls(result.config, true);
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
  if (!ConfigProfileCodec::save(fileSystemPath(path), configFromControls(), error)) {
    QMessageBox::critical(this, "Profile save failed", QString::fromStdString(error));
    return;
  }
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
  connect_button_->setEnabled(!runtime_status_.running);
  apply_button_->setEnabled(!runtime_status_.running);
  load_button_->setEnabled(!runtime_status_.running);
  save_button_->setEnabled(!runtime_status_.running);
  for (auto* control : restart_required_controls_) {
    control->setEnabled(!runtime_status_.running);
  }
  if (runtime_status_.running) {
    digitizer_lock_state_->setText("LOCKED | press STOP before setup changes");
    digitizer_lock_state_->setProperty("statusKind", "warn");
  } else if (restart_dirty_) {
    digitizer_lock_state_->setText("APPLY REQUIRED | reconnect before START");
    digitizer_lock_state_->setProperty("statusKind", "warn");
  } else {
    digitizer_lock_state_->setText("READY | board settings applied");
    digitizer_lock_state_->setProperty("statusKind", "ready");
  }
  repolish(digitizer_lock_state_);

  overview_digitizer_->setText(runtime_status_.digitizer_ready ? "READY\nSingle channel" : "NOT READY");
  overview_edfa_->setText(runtime_status_.edfa_bypassed ? "BYPASS\nNo EDFA" :
                          runtime_status_.edfa_output_enabled ? "OUTPUT ON" : "READY\nOutput off");
  overview_mcu_->setText(runtime_status_.mcu_bypassed ? "BYPASS\nMCU disabled" :
                         runtime_status_.mcu_waveform_loaded
                             ? QString("READY\n%1 points").arg(runtime_status_.mcu_waveform_points)
                             : "WAITING\nNo waveform");
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
  if (runtime_status_.udp_running) {
    udp_indicator_->setText("UDP TX");
    udp_indicator_->setProperty("statusKind", "ready");
    udp_status_->setText(QString("Sending | %1 fps | %2 packets | queue %3 / %4 | %5 dropped")
                             .arg(runtime_status_.udp_send_fps, 0, 'f', 1)
                             .arg(runtime_status_.udp_packets_sent)
                             .arg(runtime_status_.udp_queue_size)
                             .arg(runtime_status_.udp_queue_capacity)
                             .arg(runtime_status_.udp_dropped_frames));
    udp_status_->setProperty("statusKind", runtime_status_.udp_dropped_frames == 0U ? "ready" : "warn");
  } else if (udp_enabled_->isChecked()) {
    udp_indicator_->setText("UDP READY");
    udp_indicator_->setProperty("statusKind", "neutral");
    udp_status_->setText(runtime_status_.running ? "Sender stopped" : "Configured | starts with global START");
    udp_status_->setProperty("statusKind", runtime_status_.running ? "warn" : "neutral");
  } else {
    udp_indicator_->setText("UDP OFF");
    udp_indicator_->setProperty("statusKind", "neutral");
    udp_status_->setText("UDP off");
    udp_status_->setProperty("statusKind", "neutral");
  }
  repolish(udp_indicator_);
  repolish(udp_status_);
  overview_detail_->setText(QString("%1 | Config revision %2 | %3")
                                .arg(state)
                                .arg(runtime_status_.config_revision)
                                .arg(runtime_status_.detail));
  setStatusText(mcu_waveform_state_, runtime_status_.mcu_bypassed ? "MCU bypass active" :
                runtime_status_.mcu_waveform_loaded
                    ? QString("Loaded | %1 points | %2 ms/frame")
                          .arg(runtime_status_.mcu_waveform_points)
                          .arg(runtime_status_.mcu_frame_time_ms, 0, 'f', 3)
                    : "Upload required",
                runtime_status_.mcu_ready, runtime_status_.mcu_bypassed);
  updateDerivedAcquisitionLabels();
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
