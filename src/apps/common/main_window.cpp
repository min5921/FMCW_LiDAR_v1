#include "apps/common/main_window.h"

#include "apps/common/replay_setup_loader.h"

#include "core/app_version.h"
#include "core/config_profile.h"
#include "core/config_validation.h"
#include "core/digitizer_capabilities.h"
#include "drivers/alazar/alazar_digitizer.h"
#include "drivers/mcu/mcu_protocol.h"
#include "drivers/serial/serial_transport.h"

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
#include <QLocale>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>

namespace fmcw {
namespace {

constexpr int kOverviewPageIndex = 0;
constexpr int kLivePageIndex = 1;
constexpr int kLaserEdfaPageIndex = 3;
constexpr int kScanMcuPageIndex = 4;
constexpr int kProcessingPageIndex = 5;
constexpr qint64 kFrameRateSampleIntervalMs = 1000;

enum class SerialPortRole {
  Mcu,
  Edfa,
};

QGroupBox* groupBox(const QString& title, QWidget* parent = nullptr) {
  auto* group = new QGroupBox(title, parent);
  group->setObjectName("surfaceGroup");
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
  frame->setObjectName("statusCard");
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(16, 13, 16, 13);
  layout->setSpacing(7);
  auto* caption = new QLabel(title, frame);
  caption->setObjectName("cardCaption");
  value = new QLabel("--", frame);
  value->setObjectName("cardMetric");
  value->setWordWrap(true);
  layout->addWidget(caption);
  layout->addWidget(value);
  return frame;
}

template <typename Snapshot>
std::shared_ptr<const std::vector<float>> sharedPlotValues(
    const std::shared_ptr<const Snapshot>& snapshot, const std::vector<float>& values) {
  return std::shared_ptr<const std::vector<float>>(snapshot, &values);
}

std::shared_ptr<const std::vector<float>> ownedPlotValues(std::vector<float> values) {
  return std::make_shared<const std::vector<float>>(std::move(values));
}

void repolish(QWidget* widget) {
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  widget->update();
}

void setStyledProperty(QWidget* widget, const char* name, const char* value) {
  const auto property_value = QString::fromLatin1(value);
  if (widget->property(name).toString() == property_value) {
    return;
  }
  widget->setProperty(name, property_value);
  repolish(widget);
}

QWidget* wrapInScrollArea(QWidget* content) {
  auto* scroll = new QScrollArea;
  scroll->setObjectName("pageScroll");
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->viewport()->setObjectName("pageScrollViewport");
  scroll->viewport()->setAutoFillBackground(true);
  content->setObjectName("pageContent");
  content->setAutoFillBackground(true);
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

bool isJetsonPlatform(const QString& platform_name) {
  return platform_name.compare(QStringLiteral("Jetson"), Qt::CaseInsensitive) == 0;
}

bool isUsbSerialPort(const QString& port) {
  return port.startsWith(QStringLiteral("/dev/ttyUSB")) ||
      port.startsWith(QStringLiteral("/dev/ttyACM"));
}

int serialPortPriority(const QString& port, const QString& platform_name,
                       SerialPortRole role) {
  if (!isJetsonPlatform(platform_name)) {
    return 0;
  }
  if (role == SerialPortRole::Mcu) {
    if (port == QStringLiteral("/dev/ttyTHS0")) {
      return 0;
    }
    if (port.startsWith(QStringLiteral("/dev/ttyTHS"))) {
      return 1;
    }
    return isUsbSerialPort(port) ? 2 : 3;
  }
  if (isUsbSerialPort(port)) {
    return 0;
  }
  return port.startsWith(QStringLiteral("/dev/ttyTHS")) ? 2 : 1;
}

QString serialPortDisplayText(const QString& port, const QString& platform_name) {
  if (!isJetsonPlatform(platform_name)) {
    return port;
  }
  if (port == QStringLiteral("/dev/ttyTHS0")) {
    return QStringLiteral("Jetson 40-pin UART (pins 8/10) | %1").arg(port);
  }
  if (port.startsWith(QStringLiteral("/dev/ttyTHS"))) {
    return QStringLiteral("Jetson onboard UART | %1").arg(port);
  }
  if (port.startsWith(QStringLiteral("/dev/ttyUSB"))) {
    return QStringLiteral("USB UART | %1").arg(port);
  }
  if (port.startsWith(QStringLiteral("/dev/ttyACM"))) {
    return QStringLiteral("USB CDC UART | %1").arg(port);
  }
  return port;
}

void populateSerialPortCombo(QComboBox* combo, std::vector<std::string> ports,
                             QString preferred_port, const QString& platform_name,
                             SerialPortRole role) {
  std::stable_sort(ports.begin(), ports.end(), [&](const std::string& lhs,
                                                   const std::string& rhs) {
    const auto lhs_port = QString::fromStdString(lhs);
    const auto rhs_port = QString::fromStdString(rhs);
    const auto lhs_priority = serialPortPriority(lhs_port, platform_name, role);
    const auto rhs_priority = serialPortPriority(rhs_port, platform_name, role);
    return lhs_priority == rhs_priority ? lhs_port < rhs_port : lhs_priority < rhs_priority;
  });

  const QSignalBlocker blocker(combo);
  combo->clear();
  combo->addItem(QStringLiteral("Select serial port"), QString());
  for (const auto& port : ports) {
    const auto value = QString::fromStdString(port);
    combo->addItem(serialPortDisplayText(value, platform_name), value);
  }

  int selected_index = preferred_port.isEmpty() ? 0 : combo->findData(preferred_port);
  if (!preferred_port.isEmpty() && selected_index < 0) {
    combo->addItem(QStringLiteral("%1 | not detected")
                       .arg(serialPortDisplayText(preferred_port, platform_name)),
                   preferred_port);
    selected_index = combo->count() - 1;
  }
  combo->setCurrentIndex(std::max(selected_index, 0));
  combo->setToolTip(QStringLiteral("%1 serial port(s) detected")
                        .arg(static_cast<qulonglong>(ports.size())));
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

QString inputRangeText(double input_range_volts) {
  if (input_range_volts >= 1.0) {
    return QString("+/- %1 V").arg(input_range_volts, 0, 'g', 6);
  }
  return QString("+/- %1 mV").arg(input_range_volts * 1000.0, 0, 'g', 6);
}

class RecordLengthSpinBox final : public QSpinBox {
 public:
  explicit RecordLengthSpinBox(QWidget* parent = nullptr) : QSpinBox(parent) {
    setRange(0, 16 * 1024 * 1024);
    setKeyboardTracking(false);
    setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
  }

  void setBoardCapabilities(const DigitizerBoardCapabilities& capabilities) {
    capabilities_ = &capabilities;
    const auto resolution = std::max<std::uint32_t>(capabilities.record_resolution_samples, 1U);
    const auto maximum_supported = static_cast<std::uint32_t>(maximum()) -
        (static_cast<std::uint32_t>(maximum()) % resolution);
    setRange(static_cast<int>(capabilities.minimum_record_samples),
             static_cast<int>(maximum_supported));
    setSingleStep(static_cast<int>(resolution));
    setSupportedValue(static_cast<std::uint32_t>(value()));
    setToolTip(QString("%1 record length: minimum %2, exact multiples of %3 only")
                   .arg(QString::fromStdString(capabilities.display_name))
                   .arg(capabilities.minimum_record_samples)
                   .arg(capabilities.record_resolution_samples));
  }

  void setSupportedValue(std::uint32_t requested_samples) {
    if (capabilities_ == nullptr) {
      QSpinBox::setValue(static_cast<int>(std::min<std::uint32_t>(
          requested_samples, static_cast<std::uint32_t>(maximum()))));
      return;
    }
    const auto supported = nearestSupportedRecordLength(*capabilities_, requested_samples);
    QSpinBox::setValue(static_cast<int>(std::clamp<std::uint32_t>(
        supported, static_cast<std::uint32_t>(minimum()), static_cast<std::uint32_t>(maximum()))));
  }

 protected:
  QValidator::State validate(QString& input, int& position) const override {
    const auto base_state = QSpinBox::validate(input, position);
    if (base_state == QValidator::Invalid || capabilities_ == nullptr) {
      return base_state;
    }
    bool parsed = false;
    const auto candidate = locale().toLongLong(input.trimmed(), &parsed);
    if (!parsed || candidate < minimum()) {
      return QValidator::Intermediate;
    }
    if (candidate > maximum()) {
      return QValidator::Invalid;
    }
    return supportsRecordLength(*capabilities_, static_cast<std::uint32_t>(candidate))
        ? QValidator::Acceptable
        : QValidator::Intermediate;
  }

  void fixup(QString& input) const override {
    if (capabilities_ == nullptr) {
      QSpinBox::fixup(input);
      return;
    }
    bool parsed = false;
    const auto requested = locale().toLongLong(input.trimmed(), &parsed);
    const auto bounded = parsed && requested > 0
        ? static_cast<std::uint32_t>(std::min<qlonglong>(requested, maximum()))
        : static_cast<std::uint32_t>(value());
    const auto supported = nearestSupportedRecordLength(*capabilities_, bounded);
    input = locale().toString(static_cast<int>(std::clamp<std::uint32_t>(
        supported, static_cast<std::uint32_t>(minimum()), static_cast<std::uint32_t>(maximum()))));
  }

 private:
  const DigitizerBoardCapabilities* capabilities_ = nullptr;
};

QString darkStyleSheet() {
  return QStringLiteral(R"(
    QWidget { font-family: "Segoe UI"; font-size: 9pt; }
    QMainWindow, QDialog, QMessageBox, QWidget#workspace, QWidget#pageSurface,
    QWidget#pageContent, QStackedWidget, QScrollArea#pageScroll,
    QWidget#pageScrollViewport { background-color: #11171a; color: #dce5e7; }
    QWidget#sidebar { background: #0b1013; color: #edf4f3; }
    QLabel#brand { color: #f6f9f9; font-size: 16pt; font-weight: 700; }
    QLabel#platform { color: #61c7ba; font-size: 8pt; font-weight: 600; }
    QListWidget#navigation { background: transparent; border: none; outline: none; color: #aebcc0; }
    QListWidget#navigation::item { height: 42px; padding-left: 12px; border-left: 3px solid transparent; }
    QListWidget#navigation::item:hover { background: #182226; color: #ffffff; }
    QListWidget#navigation::item:selected { background: #203036; color: #ffffff; border-left: 3px solid #35b2a3; }
    QWidget#commandBar { background: #171f23; border-bottom: 1px solid #303b40; }
    QLabel#pageTitle { color: #f0f4f5; font-size: 17pt; font-weight: 650; }
    QLabel[caption="true"], QLabel#cardCaption { color: #91a2a8; font-size: 8pt; font-weight: 600; }
    QLabel#cardMetric { color: #e8eff0; font-size: 12pt; font-weight: 650; }
    QFrame#statusCard, QGroupBox#surfaceGroup {
      background-color: #192226;
      border: 1px solid #303c41;
      border-radius: 6px;
    }
    QGroupBox { margin-top: 12px; padding: 14px 12px 10px 12px; font-weight: 650; }
    QGroupBox::title {
      subcontrol-origin: margin;
      left: 12px;
      padding: 0 5px;
      color: #cbd6d9;
    }
    QPushButton {
      min-height: 30px;
      padding: 0 12px;
      border: 1px solid #3a494f;
      border-radius: 4px;
      background: #202b30;
      color: #dce5e7;
      font-weight: 600;
    }
    QPushButton:hover { background: #29373c; border-color: #61757d; }
    QPushButton:pressed { background: #152024; }
    QPushButton:disabled { color: #68777c; background: #181f22; border-color: #2c363a; }
    QPushButton#applyButton { background: #173d3a; color: #7ee0d3; border-color: #347e76; }
    QPushButton#applyButton:hover { background: #1d4b47; border-color: #4da398; }
    QPushButton#connectButton { background: #31545c; color: #ffffff; border-color: #426b74; }
    QPushButton#startButton[runState="start"] { min-width: 76px; background: #19734f; color: #ffffff; border-color: #249169; }
    QPushButton#startButton[runState="stop"] { min-width: 76px; background: #bd5f29; color: #ffffff; border-color: #df7d43; }
    QPushButton#startButton[runState="stopping"] { min-width: 76px; background: #4a3d28; color: #cfc4ae; border-color: #6a593b; }
    QPushButton#emergencyButton { background: #a82f38; color: #ffffff; border-color: #ca4c55; }
    QToolButton {
      min-width: 30px;
      min-height: 30px;
      border: 1px solid #3a494f;
      border-radius: 4px;
      background: #202b30;
      color: #dce5e7;
    }
    QToolButton:hover, QToolButton:checked { background: #21403e; border-color: #4b9f96; }
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
      min-height: 28px;
      padding: 0 7px;
      background: #12191d;
      color: #e1e8ea;
      border: 1px solid #3a494f;
      border-radius: 3px;
      selection-background-color: #277f76;
    }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #35b2a3; }
    QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background: #171e21; color: #708086; border-color: #2a3438; }
    QComboBox QAbstractItemView { background: #182125; color: #e1e8ea; border: 1px solid #425158; selection-background-color: #275f5a; }
    QCheckBox { color: #d6dfe1; spacing: 7px; }
    QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #53666d; border-radius: 2px; background: #12191d; }
    QCheckBox::indicator:checked { background: #2da899; border-color: #55cbbd; }
    QSlider::groove:horizontal { height: 4px; background: #344248; border-radius: 2px; }
    QSlider::sub-page:horizontal { background: #35b2a3; border-radius: 2px; }
    QSlider::handle:horizontal { width: 14px; margin: -5px 0; background: #dce7e8; border: 1px solid #54c1b4; border-radius: 7px; }
    QProgressBar {
      min-height: 22px;
      border: 1px solid #3d4c52;
      border-radius: 3px;
      background: #12191d;
      color: #dce5e7;
      text-align: center;
      font-weight: 600;
    }
    QProgressBar::chunk { background: #238b7b; }
    QTabWidget::pane { border: 1px solid #303c41; background: #192226; }
    QWidget#plotSurface { background-color: #151e22; color: #dce5e7; }
    QTabBar::tab {
      min-width: 106px;
      height: 31px;
      padding: 0 10px;
      background: #141c20;
      color: #8fa0a6;
      border: 1px solid #303c41;
    }
    QTabBar::tab:hover { background: #202b30; color: #dce5e7; }
    QTabBar::tab:selected { background: #192226; color: #62d0c2; border-top: 2px solid #35b2a3; }
    QLabel[statusKind], QPushButton#validationButton {
      border: 1px solid #46555b;
      border-radius: 3px;
      padding: 3px 7px;
      font-weight: 600;
    }
    QLabel[statusKind="ready"], QPushButton#validationButton[statusKind="ready"] { background: #153a2b; color: #79d9ad; border-color: #286a50; }
    QLabel[statusKind="warn"], QPushButton#validationButton[statusKind="warn"] { background: #473619; color: #f2c873; border-color: #80632d; }
    QLabel[statusKind="error"], QPushButton#validationButton[statusKind="error"] { background: #452125; color: #f0959c; border-color: #7b3940; }
    QLabel[statusKind="neutral"], QPushButton#validationButton[statusKind="neutral"] { background: #263137; color: #afbdc1; border-color: #46555b; }
    QPushButton#validationButton { min-height: 28px; padding: 0 9px; }
    QPlainTextEdit {
      background: #0c1114;
      color: #d5e0e2;
      border: 1px solid #303c41;
      font-family: Consolas;
      font-size: 9pt;
    }
    QScrollArea#pageScroll, QScrollArea#pageScroll > QWidget#pageScrollViewport,
    QScrollArea#pageScroll QWidget#pageContent { background-color: #11171a; }
    QScrollBar:vertical { background: #11171a; width: 11px; margin: 0; }
    QScrollBar::handle:vertical { background: #405057; min-height: 28px; border-radius: 4px; }
    QScrollBar::handle:vertical:hover { background: #586b73; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QScrollBar:horizontal { background: #11171a; height: 11px; margin: 0; }
    QScrollBar::handle:horizontal { background: #405057; min-width: 28px; border-radius: 4px; }
    QScrollBar::handle:horizontal:hover { background: #586b73; }
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
    QSplitter::handle { background: #303c41; }
    QMenu { background: #182125; color: #e1e8ea; border: 1px solid #425158; }
    QMenu::item:selected { background: #275f5a; }
    QStatusBar { background: #151d21; color: #91a2a8; border-top: 1px solid #303b40; }
    QToolTip { background: #263238; color: #f2f6f6; border: 1px solid #52646b; padding: 4px; }
  )");
}

QPalette darkPalette() {
  QPalette palette;
  const auto set_enabled_colors = [&palette](QPalette::ColorGroup group) {
    palette.setColor(group, QPalette::Window, QColor("#11171a"));
    palette.setColor(group, QPalette::WindowText, QColor("#dce5e7"));
    palette.setColor(group, QPalette::Base, QColor("#151e22"));
    palette.setColor(group, QPalette::AlternateBase, QColor("#222e33"));
    palette.setColor(group, QPalette::ToolTipBase, QColor("#263238"));
    palette.setColor(group, QPalette::ToolTipText, QColor("#f2f6f6"));
    palette.setColor(group, QPalette::Text, QColor("#dce5e7"));
    palette.setColor(group, QPalette::Button, QColor("#202b30"));
    palette.setColor(group, QPalette::ButtonText, QColor("#dce5e7"));
    palette.setColor(group, QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(group, QPalette::Light, QColor("#64777e"));
    palette.setColor(group, QPalette::Midlight, QColor("#52646b"));
    palette.setColor(group, QPalette::Mid, QColor("#35444a"));
    palette.setColor(group, QPalette::Dark, QColor("#0b1013"));
    palette.setColor(group, QPalette::Shadow, QColor("#050809"));
    palette.setColor(group, QPalette::Link, QColor("#61c7ba"));
    palette.setColor(group, QPalette::Highlight, QColor("#35b2a3"));
    palette.setColor(group, QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(group, QPalette::PlaceholderText, QColor("#82949b"));
  };
  set_enabled_colors(QPalette::Active);
  set_enabled_colors(QPalette::Inactive);
  set_enabled_colors(QPalette::Disabled);
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#68777c"));
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#68777c"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#68777c"));
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#a8b3b7"));
  return palette;
}

}  // namespace

void applyDarkApplicationTheme(QApplication& application) {
  if (auto* fusion_style = QStyleFactory::create(QStringLiteral("Fusion"))) {
    application.setStyle(fusion_style);
  }
  application.setPalette(darkPalette());
  application.setStyleSheet(darkStyleSheet());
}

MainWindow::MainWindow(QString platform_name, QWidget* parent)
    : QMainWindow(parent), platform_name_(std::move(platform_name)) {
  config_ = makeAts9371QualificationSimulatorConfig();
  if (platform_name_.compare(QStringLiteral("Jetson"), Qt::CaseInsensitive) == 0) {
    config_.ui.plot_update_hz = 30.0;
    config_.processing.fft_backend = FftBackendKind::Cuda;
    config_.mcu.port = "/dev/ttyTHS0";
  }
  setWindowTitle(QString("FMCW LiDAR v%1 - %2").arg(QString::fromStdString(versionString()), platform_name_));
  setMinimumSize(1180, 720);
  resize(1480, 900);

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
  runtime_source_badge_ = new QLabel(QString("%1  |  SIMULATOR").arg(platform_name_.toUpper()), sidebar);
  runtime_source_badge_->setObjectName("platform");
  sidebar_layout->addWidget(brand);
  sidebar_layout->addWidget(runtime_source_badge_);
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
  start_stop_button_->setFixedWidth(104);
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
  runtime_state_label_->setMinimumWidth(210);
  runtime_state_label_->setProperty("statusKind", "neutral");
  statusBar()->addWidget(runtime_state_label_);
  statusBar()->showMessage("Simulator | Up-chirp trigger | Full-period DMA batch", 0);

  stop_stage_timer_ = new QTimer(this);
  stop_stage_timer_->setInterval(100);
  connect(stop_stage_timer_, &QTimer::timeout, this, &MainWindow::updateStopStageDisplay);

  controller_ = new ApplicationController(platform_name_, this);
  loadConfigToControls(config_);
  connectUi();
  live_display_timer_ = new QTimer(this);
  live_display_timer_->setTimerType(Qt::PreciseTimer);
  live_display_timer_->setInterval(1000);
  connect(live_display_timer_, &QTimer::timeout, this, &MainWindow::updateLiveDisplayDiagnostics);
  live_display_timer_->start();
  navigation_->setCurrentRow(kOverviewPageIndex);
  validateControls();
  controller_->applyConfig(config_);
}

bool MainWindow::savePointCloudFramebuffer(const QString& path) {
  return point_cloud_plot_ != nullptr && point_cloud_plot_->grabFramebuffer().save(path, "PNG");
}

QWidget* MainWindow::makePage(QString title, QWidget* content) {
  auto* page = new QWidget;
  page->setObjectName("pageSurface");
  page->setAutoFillBackground(true);
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
  grid->addWidget(statusCard("BATCH LATENCY", overview_latency_, content), 1, 2);
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
  selected_a_scan_slider_ = new QSlider(Qt::Horizontal, content);
  selected_a_scan_slider_->setRange(0, 0);
  selected_a_scan_slider_->setTracking(false);
  selected_a_scan_slider_->setMinimumWidth(220);
  selected_a_scan_slider_->setMaximumWidth(420);
  selected_a_scan_slider_->setFixedHeight(28);
  selected_a_scan_slider_->setToolTip("Drag to select the displayed A-scan record");
  selected_a_scan_status_ = new QLabel("Waiting for selected record", content);
  selected_a_scan_status_->setProperty("statusKind", "neutral");
  tools->addWidget(selected_label);
  tools->addWidget(selected_a_scan_slider_);
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
    selected_a_scan_slider_->setValue(value);
    selected_a_scan_status_->setText(QString("Waiting for A-scan %1").arg(value));
    selected_a_scan_status_->setProperty("statusKind", "neutral");
    repolish(selected_a_scan_status_);
    controller_->setSelectedAScan(static_cast<std::uint32_t>(value));
  });
  connect(selected_a_scan_slider_, &QSlider::valueChanged,
          selected_a_scan_, &QSpinBox::setValue);
  tools->addWidget(auto_range);
  tools->addWidget(manual_range);
  tools->addWidget(freeze_button_);
  tools->addWidget(save_view);
  layout->addLayout(tools);
  live_display_diagnostics_ = new QLabel(
      "Display | DMA -- Hz | delivered -- Hz | painted -- Hz | waiting for live data", content);
  live_display_diagnostics_->setProperty("caption", true);
  live_display_diagnostics_->setWordWrap(true);
  live_display_diagnostics_->setMinimumHeight(36);
  live_display_diagnostics_->setToolTip(
      "DMA is the acquisition rate. Delivered is selected plot snapshots reaching the GUI. "
      "Painted is completed plot redraws. Omitted and merged counts are display-only and do not mean acquisition loss.");
  layout->addWidget(live_display_diagnostics_);

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
  auto* show_axes = new QCheckBox("XYZ axes", point_cloud_page);
  show_axes->setChecked(true);
  show_axes->setToolTip("Show or hide the X, Y, and Z reference axes");
  auto* reset_camera = new QToolButton(point_cloud_page);
  reset_camera->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  reset_camera->setToolTip("Fit the current cloud and reset the 3D camera");
  auto* save_cloud = new QToolButton(point_cloud_page);
  save_cloud->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_cloud->setToolTip("Save current point cloud as CSV");
  point_cloud_status_ = new QLabel("Waiting for complete raster frame", point_cloud_page);
  point_cloud_status_->setProperty("statusKind", "neutral");
  point_cloud_tools->addWidget(color_mode);
  point_cloud_tools->addWidget(new QLabel("Point size", point_cloud_page));
  point_cloud_tools->addWidget(point_size);
  point_cloud_tools->addWidget(show_axes);
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
  live_tabs_->addTab(bscan_plot_, "B-scan | X forward depth");
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
      statusBar()->showMessage("Use Fit View to update the fixed 3D spatial bounds", 3000);
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
  connect(show_axes, &QCheckBox::toggled, point_cloud_plot_, &PointCloudWidget::setAxesVisible);
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
  auto* board = groupBox("Acquisition Source / Board", content);
  auto* board_form = new QFormLayout(board);
  tuneForm(board_form);
  acquisition_source_ = new QComboBox(board);
  acquisition_source_->addItem("Simulator", static_cast<int>(AcquisitionSource::Simulator));
  acquisition_source_->addItem("AlazarTech ATS", static_cast<int>(AcquisitionSource::Alazar));
  acquisition_source_->addItem("Raw Replay", static_cast<int>(AcquisitionSource::Replay));
  replay_file_ = new QLineEdit(board);
  replay_file_->setPlaceholderText("Select *.raw.0000.bin");
  replay_browse_ = new QToolButton(board);
  replay_browse_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  replay_browse_->setToolTip("Select raw replay file");
  auto* replay_path = new QWidget(board);
  auto* replay_path_layout = new QHBoxLayout(replay_path);
  replay_path_layout->setContentsMargins(0, 0, 0, 0);
  replay_path_layout->setSpacing(6);
  replay_path_layout->addWidget(replay_file_, 1);
  replay_path_layout->addWidget(replay_browse_);
  replay_loop_ = new QCheckBox("Loop at end", board);
  board_model_ = new QLabel("Detecting board", board);
  board_model_->setWordWrap(true);
  board_model_->setProperty("statusKind", "neutral");
  board_model_->setToolTip(
      "Alazar hardware is detected automatically at System 1 / Board 1. "
      "Simulator and replay use the model stored in the loaded profile.");
  board_address_ = new QLabel("System 1 / Board 1 | fixed", board);
  board_address_->setWordWrap(true);
  board_address_->setProperty("statusKind", "neutral");
  digitizer_channel_ = new QComboBox(board);
  digitizer_channel_->addItems({"Channel A", "Channel B"});
  sample_rate_ = new QComboBox(board);
  const auto& default_capabilities = digitizerBoardCapabilities().front();
  auto* record_length = new RecordLengthSpinBox(board);
  record_length->setBoardCapabilities(default_capabilities);
  sample_point_ = record_length;
  record_length_state_ = new QLabel("ATS CHECK", board);
  record_length_state_->setWordWrap(true);
  record_length_state_->setProperty("statusKind", "neutral");
  input_range_ = new QComboBox(board);
  impedance_ = new QComboBox(board);
  coupling_ = new QComboBox(board);
  coupling_->addItem("DC");
  digitizer_lock_state_ = new QLabel("STOPPED | settings can be applied", board);
  digitizer_lock_state_->setProperty("statusKind", "neutral");
  board_form->addRow("Runtime source", acquisition_source_);
  board_form->addRow("Replay file", replay_path);
  board_form->addRow("Replay mode", replay_loop_);
  board_form->addRow("Board model", board_model_);
  board_form->addRow("Board address", board_address_);
  board_form->addRow("Input channel", digitizer_channel_);
  board_form->addRow("Sampling rate", sample_rate_);
  board_form->addRow("Record samples", sample_point_);
  board_form->addRow("Record check", record_length_state_);
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
  trigger_input_ = new QLabel("TRIG IN | External TTL range | level 150 | DC coupled", dma);
  trigger_input_->setWordWrap(true);
  trigger_input_->setProperty("statusKind", "neutral");
  trigger_slope_ = new QComboBox(dma);
  trigger_slope_->addItems({"Rising edge", "Falling edge"});
  trigger_delay_ = new QSpinBox(dma);
  trigger_delay_->setRange(0, 9999999);
  trigger_delay_->setSingleStep(static_cast<int>(
      default_capabilities.single_channel_trigger_delay_alignment_samples));
  trigger_delay_->setSuffix(" samples");
  pre_trigger_ = new QSpinBox(dma);
  pre_trigger_->setRange(0, static_cast<int>(default_capabilities.maximum_npt_pretrigger_samples));
  pre_trigger_->setSingleStep(static_cast<int>(default_capabilities.pretrigger_alignment_samples));
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
  dma_form->addRow("Trigger input", trigger_input_);
  dma_form->addRow("Trigger edge", trigger_slope_);
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
  restart_required_controls_.append(QList<QWidget*>{acquisition_source_, replay_file_, replay_browse_, replay_loop_,
      digitizer_channel_, sample_rate_, sample_point_, input_range_, impedance_, coupling_,
      records_per_buffer_, dma_buffer_count_, trigger_slope_,
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
  sweep_bandwidth_ = new QDoubleSpinBox(laser);
  sweep_bandwidth_->setRange(1.0, 1.0e12);
  sweep_bandwidth_->setDecimals(0);
  sweep_bandwidth_->setSingleStep(1.0e6);
  sweep_bandwidth_->setSuffix(" Hz");
  sweep_bandwidth_->setToolTip("Measured optical sweep bandwidth used for distance conversion");
  sweep_rate_ = new QDoubleSpinBox(laser);
  sweep_rate_->setRange(1.0, 1.0e9);
  sweep_rate_->setDecimals(0);
  sweep_rate_->setSingleStep(1000.0);
  sweep_rate_->setSuffix(" Hz");
  sweep_rate_->setToolTip("Measured full triangular sweep repetition rate used for distance conversion");
  laser_form->addRow("Sweep bandwidth", sweep_bandwidth_);
  laser_form->addRow("Sweep rate", sweep_rate_);

  auto* edfa = groupBox("Optional EDFA", content);
  auto* edfa_form = new QFormLayout(edfa);
  tuneForm(edfa_form);
  edfa_mode_ = new QComboBox(edfa);
  edfa_mode_->addItems({"None / Bypass", "Manual", "Controlled"});
  auto* edfa_port_row = new QWidget(edfa);
  auto* edfa_port_layout = new QHBoxLayout(edfa_port_row);
  edfa_port_layout->setContentsMargins(0, 0, 0, 0);
  edfa_port_layout->setSpacing(6);
  edfa_port_ = new QComboBox(edfa_port_row);
  edfa_port_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  edfa_port_refresh_ = new QToolButton(edfa_port_row);
  edfa_port_refresh_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  edfa_port_refresh_->setToolTip("Refresh detected serial ports");
  edfa_port_layout->addWidget(edfa_port_, 1);
  edfa_port_layout->addWidget(edfa_port_refresh_);
  refreshEdfaSerialPorts(QString::fromStdString(config_.edfa.port));
  edfa_control_mode_ = new QComboBox(edfa);
  edfa_control_mode_->addItem("APC | Output power", static_cast<int>(EdfaControlMode::Apc));
  edfa_setpoint_ = new QDoubleSpinBox(edfa);
  edfa_setpoint_->setRange(kEdfaMinimumOutputDbm, kEdfaMaximumOutputDbm);
  edfa_setpoint_->setDecimals(1);
  edfa_setpoint_->setSuffix(" dBm");
  edfa_warmup_ = new QSpinBox(edfa);
  edfa_warmup_->setRange(0, 60000);
  edfa_warmup_->setSuffix(" ms");
  edfa_output_button_ = new QPushButton("Enable Output", edfa);
  edfa_output_button_->setToolTip("Independent optical output safety control");
  auto statusLabel = [edfa] {
    auto* label = new QLabel("Waiting for connection", edfa);
    label->setWordWrap(true);
    label->setProperty("statusKind", "neutral");
    return label;
  };
  edfa_connection_status_ = statusLabel();
  edfa_activation_status_ = statusLabel();
  edfa_target_status_ = statusLabel();
  edfa_input_status_ = statusLabel();
  edfa_output_status_ = statusLabel();
  edfa_current_status_ = statusLabel();
  edfa_form->addRow("Mode", edfa_mode_);
  edfa_form->addRow("Serial port", edfa_port_row);
  edfa_form->addRow("Control", edfa_control_mode_);
  edfa_form->addRow("Output setpoint", edfa_setpoint_);
  edfa_form->addRow("Warm-up", edfa_warmup_);
  edfa_form->addRow("Optical output", edfa_output_button_);
  edfa_form->addRow("Connection", edfa_connection_status_);
  edfa_form->addRow("Soft activation", edfa_activation_status_);
  edfa_form->addRow("Device target", edfa_target_status_);
  edfa_form->addRow("Input power", edfa_input_status_);
  edfa_form->addRow("Output power", edfa_output_status_);
  edfa_form->addRow("Pump current", edfa_current_status_);
  layout->addWidget(laser, 0, 0);
  layout->addWidget(edfa, 0, 1);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  restart_required_controls_.append(QList<QWidget*>{sweep_bandwidth_, sweep_rate_,
      edfa_mode_, edfa_port_, edfa_port_refresh_, edfa_control_mode_, edfa_setpoint_, edfa_warmup_});
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
  x_start_->setToolTip("Azimuth assigned to the minimum fast-axis command");
  x_end_->setToolTip("Azimuth assigned to the maximum fast-axis command");
  y_start_->setToolTip("Elevation assigned to the minimum slow-axis command");
  y_end_->setToolTip("Elevation assigned to the maximum slow-axis command");
  geometry_form->addRow("Azimuth at fast min", x_start_);
  geometry_form->addRow("Azimuth at fast max", x_end_);
  geometry_form->addRow("Elevation at slow min", y_start_);
  geometry_form->addRow("Elevation at slow max", y_end_);
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
  auto* mcu_port_row = new QWidget(mcu);
  auto* mcu_port_layout = new QHBoxLayout(mcu_port_row);
  mcu_port_layout->setContentsMargins(0, 0, 0, 0);
  mcu_port_layout->setSpacing(6);
  mcu_port_ = new QComboBox(mcu_port_row);
  mcu_port_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  mcu_port_refresh_ = new QToolButton(mcu_port_row);
  mcu_port_refresh_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  mcu_port_refresh_->setToolTip("Refresh detected serial ports");
  mcu_port_layout->addWidget(mcu_port_, 1);
  mcu_port_layout->addWidget(mcu_port_refresh_);
  refreshMcuSerialPorts(QString::fromStdString(config_.mcu.port));
  mcu_waveform_source_ = new QComboBox(mcu);
  mcu_waveform_source_->addItem("Legacy X/Y/M file",
                                static_cast<int>(McuWaveformSource::LegacyXymFile));
  mcu_waveform_source_->addItem("Generated raster",
                                static_cast<int>(McuWaveformSource::GeneratedRaster));
  auto* waveform_file_row = new QWidget(mcu);
  auto* waveform_file_layout = new QHBoxLayout(waveform_file_row);
  waveform_file_layout->setContentsMargins(0, 0, 0, 0);
  waveform_file_layout->setSpacing(6);
  mcu_waveform_file_ = new QLineEdit(waveform_file_row);
  mcu_waveform_file_->setPlaceholderText("Select a text file: sps, then X Y M rows");
  mcu_waveform_browse_ = new QToolButton(waveform_file_row);
  mcu_waveform_browse_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
  mcu_waveform_browse_->setToolTip("Select legacy X/Y/M waveform file");
  waveform_file_layout->addWidget(mcu_waveform_file_, 1);
  waveform_file_layout->addWidget(mcu_waveform_browse_);
  mcu_trigger_shift_ = new QSpinBox(mcu);
  mcu_trigger_shift_->setRange(-14999, 14999);
  mcu_trigger_shift_->setSuffix(" samples");
  mcu_trigger_shift_->setToolTip(
      "Fine adjustment applied only to the uploaded M/B-trigger markers. "
      "Negative advances and positive delays the trigger; one MCU sample is 10 us. "
      "The emitted marker remains the acquisition and coordinate line anchor.");
  mcu_point_rate_ = new QLabel("100 kHz | firmware TIM6", mcu);
  mcu_point_rate_->setProperty("statusKind", "neutral");
  mcu_frame_time_ = new QLabel("16.000 ms | 1600 points / frame", mcu);
  mcu_frame_time_->setProperty("statusKind", "neutral");
  frame_sync_state_ = new QLabel("Waiting for DMA timing", mcu);
  frame_sync_state_->setProperty("statusKind", "neutral");
  upload_waveform_button_ = new QPushButton("Upload Waveform", mcu);
  mcu_upload_progress_ = new QProgressBar(mcu);
  mcu_upload_progress_->setRange(0, 100);
  mcu_upload_progress_->setValue(0);
  mcu_upload_progress_->setFormat("Idle");
  mcu_waveform_state_ = new QLabel("MCU bypass active", mcu);
  mcu_waveform_state_->setWordWrap(true);
  mcu_waveform_state_->setProperty("statusKind", "neutral");
  mcu_form->addRow("Controller", mcu_enabled_);
  mcu_form->addRow("Serial port", mcu_port_row);
  mcu_form->addRow("Waveform source", mcu_waveform_source_);
  mcu_form->addRow("X/Y/M file", waveform_file_row);
  mcu_form->addRow("B-trigger offset", mcu_trigger_shift_);
  mcu_form->addRow("Point rate", mcu_point_rate_);
  mcu_form->addRow("Cycle / waveform", mcu_frame_time_);
  mcu_form->addRow("DMA / MCU sync", frame_sync_state_);
  mcu_form->addRow("Full-frame waveform", upload_waveform_button_);
  mcu_form->addRow("Upload progress", mcu_upload_progress_);
  mcu_form->addRow("Current status", mcu_waveform_state_);
  layout->addWidget(geometry, 0, 0);
  layout->addWidget(mcu, 0, 1);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  restart_required_controls_.append(QList<QWidget*>{x_start_, x_end_, y_start_, y_end_, y_lines_, bidirectional_,
      mcu_enabled_, mcu_port_, mcu_port_refresh_, mcu_waveform_source_, mcu_waveform_file_,
      mcu_waveform_browse_, mcu_trigger_shift_,
      upload_waveform_button_});
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
  const bool is_jetson =
      platform_name_.compare(QStringLiteral("Jetson"), Qt::CaseInsensitive) == 0;
  if (is_jetson) {
    fft_backend_->addItem("CUDA cuFFT");
    fft_backend_->setEnabled(false);
  } else {
    fft_backend_->addItems({"FFTW (CPU)", "CUDA cuFFT"});
  }
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

  auto* realtime = groupBox("Batch Diagnostics", content);
  auto* realtime_form = new QFormLayout(realtime);
  tuneForm(realtime_form);
  batch_workload_ = new QLabel("4992 samples x 998 records", realtime);
  batch_workload_->setWordWrap(true);
  batch_latency_ = new QLabel("Waiting for first complete batch", realtime);
  batch_latency_->setWordWrap(true);
  batch_percentiles_ = new QLabel("p50 -- | p95 -- | p99 -- | max --", realtime);
  batch_percentiles_->setWordWrap(true);
  batch_deadline_ = new QLabel("5.000 ms deadline", realtime);
  batch_deadline_->setWordWrap(true);
  batch_deadline_->setProperty("statusKind", "neutral");
  realtime_form->addRow("Workload", batch_workload_);
  realtime_form->addRow("Last", batch_latency_);
  realtime_form->addRow("Latency", batch_percentiles_);
  realtime_form->addRow("Deadline", batch_deadline_);

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
  period_start_ = segmentSpin();
  up_start_ = segmentSpin();
  up_length_ = segmentSpin();
  down_start_ = segmentSpin();
  down_length_ = segmentSpin();
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
  addControl("Period start", period_start_);
  addControl("UP start", up_start_);
  addControl("UP length", up_length_);
  addControl("DOWN start", down_start_);
  addControl("DOWN length", down_length_);
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
  layout->addWidget(realtime, 0, 2);
  layout->addWidget(segmentation, 1, 0, 1, 3);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 1);
  layout->setColumnStretch(2, 1);
  layout->setRowStretch(1, 1);
  connect(capture, &QPushButton::clicked, this, [this] { controller_->captureSegmentationSnapshot(); });
  connect(update_runtime, &QPushButton::clicked, this, [this] {
    if (!validateControls(true)) {
      return;
    }
    const auto updated = configFromControls();
    controller_->updateProcessing(updated.processing);
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
  storage_status_ = new QLabel("Storage idle", storage);
  storage_status_->setWordWrap(true);
  storage_status_->setProperty("statusKind", "neutral");
  storage_form->addRow("Raw", raw_enabled_);
  storage_form->addRow("Processed", processed_enabled_);
  storage_form->addRow("Output directory", path_row);
  storage_form->addRow("Writer queue", storage_queue_);
  storage_form->addRow("Split size", split_size_);
  storage_form->addRow("Runtime", storage_status_);

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
  udp_version_->addItem("v2 | ROS XYZ | little endian", 2);
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
  connect(navigation_, &QListWidget::currentRowChanged, this,
          [this](int page_index) {
            updateLivePlotSubscription();
            if (page_index == kScanMcuPageIndex) {
              if (!runtime_status_.running) {
                refreshMcuSerialPorts(mcu_port_->currentData().toString());
              }
              updateDerivedAcquisitionLabels();
            }
            if (page_index == kLaserEdfaPageIndex && !runtime_status_.running) {
              refreshEdfaSerialPorts(edfa_port_->currentData().toString());
              updateEdfaStatusDisplay();
            }
            if (page_index == kProcessingPageIndex) {
              updateProcessingTelemetryLabels();
            }
          });
  connect(live_tabs_, &QTabWidget::currentChanged, this,
          [this] { updateLivePlotSubscription(); });
  connect(load_button_, &QToolButton::clicked, this, &MainWindow::loadProfile);
  connect(save_button_, &QToolButton::clicked, this, &MainWindow::saveProfile);
  connect(apply_button_, &QPushButton::clicked, this, &MainWindow::applyProfile);
  connect(validation_button_, &QPushButton::clicked, this, &MainWindow::showValidationDetails);
  connect(connect_button_, &QPushButton::clicked, this, [this] {
    if (runtime_status_.connected) {
      controller_->disconnectSystem();
      return;
    }
    if (static_cast<AcquisitionSource>(acquisition_source_->currentData().toInt()) ==
            AcquisitionSource::Alazar &&
        !refreshDigitizerBoardModel()) {
      QMessageBox::warning(this, "Alazar board detection", alazar_detection_detail_);
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
    if (mcu_uploading_) {
      statusBar()->showMessage("START is locked until the MCU waveform upload finishes", 5000);
      return;
    }
    if (config_dirty_) {
      statusBar()->showMessage("Apply Setup before START", 5000);
      return;
    }
    if (validateControls(true)) {
      controller_->startSystem();
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
    const bool enable = !runtime_status_.edfa_output_enabled;
    if (enable) {
      const auto answer = QMessageBox::warning(
          this, "Enable EDFA optical output",
          QString("Enable APC output at %1 dBm?\n\n"
                  "Confirm the optical path is enclosed or safely terminated and the EDFA key is ON.")
              .arg(edfa_setpoint_->value(), 0, 'f', 1),
          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
      if (answer != QMessageBox::Yes) {
        return;
      }
    }
    controller_->setEdfaOutput(enable);
  });
  connect(edfa_port_refresh_, &QToolButton::clicked, this, [this] {
    refreshEdfaSerialPorts(edfa_port_->currentData().toString());
  });
  connect(edfa_mode_, &QComboBox::currentIndexChanged, this, [this] {
    updateEdfaStatusDisplay();
  });
  connect(edfa_port_, &QComboBox::currentIndexChanged, this, [this] {
    updateEdfaStatusDisplay();
  });
  connect(mcu_port_refresh_, &QToolButton::clicked, this, [this] {
    refreshMcuSerialPorts(mcu_port_->currentData().toString());
  });
  connect(upload_waveform_button_, &QPushButton::clicked, this, [this] {
    if (restart_dirty_) {
      QMessageBox::information(this, "MCU waveform",
                               "Apply Setup and reconnect before uploading the selected waveform.");
      return;
    }
    updateMcuUploadProgress(McuUploadProgress{
        McuUploadStage::Preparing, 0, 0, "Preparing configured waveform"});
    controller_->uploadMcuWaveform();
  });
  connect(freeze_button_, &QToolButton::toggled, this, [this](bool checked) {
    freeze_live_ = checked;
    freeze_button_->setIcon(style()->standardIcon(checked ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    freeze_button_->setToolTip(checked ? "Resume live plot display" : "Freeze plot display");
    updateLivePlotSubscription();
  });

  connect(controller_, &ApplicationController::statusChanged, this, &MainWindow::updateStatus);
  connect(controller_, &ApplicationController::mcuUploadProgressChanged,
          this, &MainWindow::updateMcuUploadProgress);
  connect(controller_, &ApplicationController::logMessage, this, &MainWindow::appendLog);
  connect(controller_, &ApplicationController::commandFailed, this, [this](const QString& command, const QString& message) {
    if (command == "MCU waveform" && mcu_uploading_) {
      auto failed = mcu_upload_progress_state_;
      failed.stage = McuUploadStage::Failed;
      failed.detail = message.toStdString();
      updateMcuUploadProgress(std::move(failed));
    }
    statusBar()->showMessage(QString("%1 failed: %2").arg(command, message), 8000);
  });
  connect(controller_, &ApplicationController::commandCompleted, this,
          [this](const QString& command, const QString& message) {
            statusBar()->showMessage(QString("%1: %2").arg(command, message), 5000);
            if (command == "Processing update") {
              const auto candidate = configFromControls();
              config_.processing = candidate.processing;
              controls_config_.processing = candidate.processing;
              config_dirty_ = restart_dirty_;
              validateControls();
            } else if (command == "Apply configuration" || command == "Connect" || command == "Start") {
              config_ = configFromControls();
              controls_config_ = config_;
              config_dirty_ = false;
              restart_dirty_ = false;
              if (command != "Start") {
                updateMcuUploadProgress(McuUploadProgress{});
              }
              if (!runtime_status_.running) {
                digitizer_lock_state_->setText("READY | board settings applied");
                digitizer_lock_state_->setProperty("statusKind", "ready");
                repolish(digitizer_lock_state_);
              }
              updateMcuWaveformControls();
              updateMcuStatusDisplay();
              validateControls();
            } else if (command == "MCU waveform" && mcu_uploading_) {
              auto complete = mcu_upload_progress_state_;
              complete.stage = McuUploadStage::Complete;
              complete.completed_points = complete.total_points;
              complete.detail = message.toStdString();
              updateMcuUploadProgress(std::move(complete));
            } else if (command == "Disconnect") {
              updateMcuUploadProgress(McuUploadProgress{});
            }
          });
  connect(controller_, &ApplicationController::waveformReady, this, [this](WaveformSnapshotPtr snapshot) {
    if (!isLivePlotActive(0) || snapshot == nullptr) {
      return;
    }
    time_plot_->setSeries({{"Full period", sharedPlotValues(snapshot, snapshot->full_scale_samples),
                            QColor("#167a86")}}, snapshot->dma_buffer_sequence);
    updateSelectedAScanStatus(snapshot->record_index_in_buffer,
                              snapshot->records_in_buffer,
                              snapshot->dma_buffer_sequence);
  });
  connect(controller_, &ApplicationController::fftReady, this, [this](FftSnapshotPtr snapshot) {
    if (!isLivePlotActive(1) || snapshot == nullptr) {
      return;
    }
    const auto up_display_count = snapshot->up_magnitude_db.size() > 1U
        ? snapshot->up_magnitude_db.size() - 1U : 0U;
    const auto down_display_count = snapshot->down_magnitude_db.size() > 1U
        ? snapshot->down_magnitude_db.size() - 1U : 0U;
    fft_plot_->setSeries({{"UP", sharedPlotValues(snapshot, snapshot->up_magnitude_db),
                           QColor("#188266"), up_display_count},
                          {"DOWN", sharedPlotValues(snapshot, snapshot->down_magnitude_db),
                           QColor("#d06432"), down_display_count}},
                         snapshot->dma_buffer_sequence);
    updateSelectedAScanStatus(snapshot->record_index_in_buffer,
                              snapshot->records_in_buffer,
                              snapshot->dma_buffer_sequence);
  });
  connect(controller_, &ApplicationController::scanLineReady, this, [this](ScanLineSnapshotPtr snapshot) {
    if (snapshot == nullptr) {
      return;
    }
    if (isLivePlotActive(2)) {
      peak_index_plot_->setSeries({{"UP", sharedPlotValues(snapshot, snapshot->up_peak_index), QColor("#188266")},
                                   {"DOWN", sharedPlotValues(snapshot, snapshot->down_peak_index), QColor("#d06432")}});
      peak_value_plot_->setSeries({{"UP", sharedPlotValues(snapshot, snapshot->up_peak_value_db), QColor("#188266")},
                                   {"DOWN", sharedPlotValues(snapshot, snapshot->down_peak_value_db), QColor("#d06432")}});
      return;
    }
    if (!isLivePlotActive(3)) {
      return;
    }
    auto distance = snapshot->distance_m;
    auto velocity = snapshot->velocity_mps;
    for (std::size_t index = 0; index < distance.size() && index < snapshot->valid.size(); ++index) {
      if (snapshot->valid[index] == 0U) {
        distance[index] = std::numeric_limits<float>::quiet_NaN();
        velocity[index] = std::numeric_limits<float>::quiet_NaN();
      }
    }
    distance_plot_->setSeries({{"Distance (m)", ownedPlotValues(std::move(distance)), QColor("#13737f")},
                                {"Velocity (m/s)", ownedPlotValues(std::move(velocity)), QColor("#ad4e61")}});
  });
  connect(controller_, &ApplicationController::bscanReady, this, [this](BScanSnapshotPtr snapshot) {
    if (isLivePlotActive(4) && snapshot != nullptr && snapshot->complete) {
      bscan_plot_->setData(snapshot->width, snapshot->height, snapshot->depth_m, snapshot->valid,
                           snapshot->completed_lines, snapshot->scan_frame_index);
    }
  });
  connect(controller_, &ApplicationController::pointCloudReady, this, [this](PointCloudSnapshotPtr snapshot) {
    if (!isLivePlotActive(5) || snapshot == nullptr || !snapshot->complete) {
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
    point_cloud_status_->setText(QString("Frame %1 complete | %2 points")
                                     .arg(snapshot->scan_frame_index + 1U)
                                     .arg(valid_points));
    point_cloud_status_->setProperty("statusKind", "ready");
    repolish(point_cloud_status_);
  });
  connect(controller_, &ApplicationController::segmentationSnapshotReady, this,
          [this](WaveformSnapshotPtr snapshot) {
            segmentation_plot_->setSnapshot(snapshot);
            segmentation_plot_->setSegments(
                segmentRangeFromStartAndLength(static_cast<std::uint32_t>(up_start_->value()),
                                               static_cast<std::uint32_t>(up_length_->value())),
                segmentRangeFromStartAndLength(static_cast<std::uint32_t>(down_start_->value()),
                                               static_cast<std::uint32_t>(down_length_->value())),
                static_cast<std::uint32_t>(guard_samples_->value()));
            segmentation_state_->setText(QString("Frozen frame %1 | overlay follows current controls").arg(snapshot->frame_id));
            segmentation_state_->setProperty("statusKind", "ready");
            repolish(segmentation_state_);
          });

  const QList<QObject*> config_controls = {
      acquisition_source_, replay_file_, replay_loop_, digitizer_channel_, sample_rate_,
      sample_point_, records_per_buffer_, dma_buffer_count_,
      input_range_, impedance_, coupling_, trigger_slope_, trigger_delay_, pre_trigger_,
      sweep_bandwidth_, sweep_rate_, edfa_mode_, edfa_port_,
      edfa_control_mode_, edfa_setpoint_, edfa_warmup_, x_start_, x_end_, y_start_, y_end_, y_lines_,
      bidirectional_, mcu_enabled_, mcu_port_, mcu_waveform_source_, mcu_waveform_file_, mcu_trigger_shift_,
      fft_backend_, window_function_, dc_removal_,
      peak_threshold_, peak_start_, peak_end_,
      period_start_, up_start_, up_length_, down_start_, down_length_, guard_samples_, fft_length_,
      raw_enabled_, processed_enabled_,
      output_directory_, storage_queue_, split_size_, udp_enabled_, udp_ip_, udp_port_, udp_points_, udp_version_,
      udp_queue_, udp_policy_};
  const QList<QObject*> runtime_controls = {dc_removal_, peak_threshold_, peak_start_, peak_end_};
  connect(acquisition_source_, &QComboBox::currentIndexChanged,
          this, [this] {
            if (static_cast<AcquisitionSource>(acquisition_source_->currentData().toInt()) ==
                AcquisitionSource::Alazar) {
              refreshDigitizerBoardModel();
            }
            updateRuntimeSourceControls();
          });
  connect(replay_browse_, &QToolButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, "Select raw replay", replay_file_->text(),
        "FMCW raw recording (*.raw.*.bin *.bin)");
    if (!path.isEmpty()) {
      loadReplaySetup(path);
    }
  });
  connect(replay_file_, &QLineEdit::editingFinished, this, [this] {
    const auto path = replay_file_->text().trimmed();
    if (QFileInfo::exists(path) && replayPathChanged(path, loaded_replay_setup_path_)) {
      loadReplaySetup(path);
    }
  });
  connect(mcu_waveform_source_, &QComboBox::currentIndexChanged, this, [this] {
    updateMcuWaveformControls();
    updateDerivedAcquisitionLabels();
  });
  connect(mcu_waveform_browse_, &QToolButton::clicked, this, [this] {
    const auto current_path = mcu_waveform_file_->text().trimmed();
    const auto path = QFileDialog::getOpenFileName(
        this, "Select legacy X/Y/M waveform", current_path,
        "X/Y/M waveform (*.txt *.xym);;All files (*)");
    if (!path.isEmpty()) {
      mcu_waveform_file_->setText(QFileInfo(path).absoluteFilePath());
    }
  });
  connect(records_per_buffer_, &QSpinBox::valueChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  connect(y_lines_, &QSpinBox::valueChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  connect(sample_point_, &QSpinBox::valueChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  connect(sample_rate_, &QComboBox::currentIndexChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  connect(pre_trigger_, &QSpinBox::valueChanged, this, [this] { updateDerivedAcquisitionLabels(); });
  connect(fft_length_, &QSpinBox::valueChanged, this, [this] { updatePeakBinLimits(); });
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
  segmentation_plot_->setSegments(segmentRangeFromStartAndLength(
                                       static_cast<std::uint32_t>(up_start_->value()),
                                       static_cast<std::uint32_t>(up_length_->value())),
                                   segmentRangeFromStartAndLength(
                                       static_cast<std::uint32_t>(down_start_->value()),
                                       static_cast<std::uint32_t>(down_length_->value())),
                                   static_cast<std::uint32_t>(guard_samples_->value()));
  if (navigation_ != nullptr && navigation_->currentRow() == kProcessingPageIndex) {
    updateProcessingTelemetryLabels();
  }
  updateMcuWaveformControls();
  updateMcuStatusDisplay();
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
  start_stop_button_->setEnabled(runtime_status_.state != OperationState::Stopping &&
                                 (runtime_status_.running ||
                                  (!mcu_uploading_ && errors == 0 && !config_dirty_)));
  apply_button_->setEnabled(!runtime_status_.running && !mcu_uploading_ && errors == 0);
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
  auto config = controls_config_;
  config.profile.name = profile_combo_->currentText().toStdString();
  config.runtime.acquisition_source =
      static_cast<AcquisitionSource>(acquisition_source_->currentData().toInt());
  config.runtime.replay_file = replay_file_->text().trimmed().toStdString();
  config.runtime.replay_loop = replay_loop_->isChecked();
  config.digitizer.board_profile = board_profile_id_.toStdString();
  if (const auto* capabilities =
          findDigitizerBoardCapabilities(config.digitizer.board_profile)) {
    config.digitizer.fifo_only_streaming =
        capabilities->fifo_only_streaming_supported;
  }
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
  config.digitizer.trigger_delay_samples = static_cast<std::uint32_t>(trigger_delay_->value());
  config.digitizer.pre_trigger_samples = static_cast<std::uint32_t>(pre_trigger_->value());
  config.digitizer.post_trigger_samples = config.digitizer.sample_point > config.digitizer.pre_trigger_samples
      ? config.digitizer.sample_point - config.digitizer.pre_trigger_samples
      : 0U;
  config.laser.sweep_bandwidth_hz = sweep_bandwidth_->value();
  config.laser.sweep_rate_hz = sweep_rate_->value();
  config.edfa.mode = static_cast<EdfaMode>(edfa_mode_->currentIndex());
  config.edfa.port = edfa_port_->currentData().toString().trimmed().toStdString();
  config.edfa.control_mode = EdfaControlMode::Apc;
  config.edfa.output_setpoint.value = edfa_setpoint_->value();
  config.edfa.output_setpoint.unit = OpticalPowerUnit::Dbm;
  config.edfa.output_min_dbm = kEdfaMinimumOutputDbm;
  config.edfa.output_max_dbm = kEdfaMaximumOutputDbm;
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
  config.mcu.port = mcu_port_->currentData().toString().trimmed().toStdString();
  config.mcu.waveform_source = static_cast<McuWaveformSource>(
      mcu_waveform_source_->currentData().toInt());
  config.mcu.waveform_file = mcu_waveform_file_->text().trimmed().toStdString();
  config.scan.trigger_shift_samples = mcu_trigger_shift_->value();
  config.processing.fft_backend =
      platform_name_.compare(QStringLiteral("Jetson"), Qt::CaseInsensitive) == 0
      ? FftBackendKind::Cuda
      : (fft_backend_->currentIndex() == 0 ? FftBackendKind::Fftw : FftBackendKind::Cuda);
  config.chirp_segmentation.window = static_cast<WindowFunction>(window_function_->currentIndex());
  config.chirp_segmentation.segment_fft_length = static_cast<std::uint32_t>(fft_length_->value());
  config.processing.dc_removal = dc_removal_->isChecked();
  config.processing.peak_threshold_db = peak_threshold_->value();
  config.processing.peak_search_start_bin = static_cast<std::uint32_t>(peak_start_->value());
  config.processing.peak_search_end_bin = static_cast<std::uint32_t>(peak_end_->value());
  config.chirp_segmentation.trigger_to_period_offset = period_start_->value();
  config.chirp_segmentation.up_segment = segmentRangeFromStartAndLength(
      static_cast<std::uint32_t>(up_start_->value()),
      static_cast<std::uint32_t>(up_length_->value()));
  config.chirp_segmentation.down_segment = segmentRangeFromStartAndLength(
      static_cast<std::uint32_t>(down_start_->value()),
      static_cast<std::uint32_t>(down_length_->value()));
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
  const QSignalBlocker sample_point_blocker(sample_point_);
  const QSignalBlocker pre_trigger_blocker(pre_trigger_);
  const QSignalBlocker trigger_delay_blocker(trigger_delay_);
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
  static_cast<RecordLengthSpinBox*>(sample_point_)->setBoardCapabilities(*capabilities);
  pre_trigger_->setSingleStep(static_cast<int>(capabilities->pretrigger_alignment_samples));
  trigger_delay_->setSingleStep(static_cast<int>(
      capabilities->single_channel_trigger_delay_alignment_samples));
  if (trigger_input_ != nullptr) {
    trigger_input_->setText(
        capabilities->external_trigger_range == AlazarExternalTriggerRange::Ttl
            ? "TRIG IN | External TTL range | level 150 | DC coupled"
            : "TRIG IN | External 5 V range | level 150 | DC coupled");
  }
  int preferred_rate_index = 0;
  double nearest_rate_distance = std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < capabilities->sample_rates_hz.size(); ++index) {
    const auto rate = capabilities->sample_rates_hz[index];
    sample_rate_->addItem(sampleRateText(rate), rate);
    const auto distance = std::abs(rate - preferred_rate_hz);
    if (preferred_rate_hz > 0.0 && distance < nearest_rate_distance) {
      preferred_rate_index = static_cast<int>(index);
      nearest_rate_distance = distance;
    }
  }
  int preferred_range_index = 0;
  double nearest_range_distance = std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < capabilities->input_ranges_volts.size(); ++index) {
    const auto range = capabilities->input_ranges_volts[index];
    input_range_->addItem(inputRangeText(range), range);
    const auto distance = std::abs(range - preferred_range_volts);
    if (preferred_range_volts > 0.0 && distance < nearest_range_distance) {
      preferred_range_index = static_cast<int>(index);
      nearest_range_distance = distance;
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
  const auto trigger_delay_alignment =
      static_cast<int>(capabilities->single_channel_trigger_delay_alignment_samples);
  if (trigger_delay_alignment > 0) {
    trigger_delay_->setValue(
        (trigger_delay_->value() / trigger_delay_alignment) *
        trigger_delay_alignment);
  }
  const auto pretrigger_alignment =
      static_cast<int>(capabilities->pretrigger_alignment_samples);
  if (pretrigger_alignment > 0) {
    pre_trigger_->setValue(
        (pre_trigger_->value() / pretrigger_alignment) * pretrigger_alignment);
  }
  updateDerivedAcquisitionLabels();
}

bool MainWindow::refreshDigitizerBoardModel() {
  if (static_cast<AcquisitionSource>(acquisition_source_->currentData().toInt()) !=
      AcquisitionSource::Alazar) {
    alazar_board_detected_ = false;
    alazar_detection_detail_.clear();
    updateDigitizerBoardDisplay();
    return true;
  }

  const auto detection = AlazarDigitizer::detectConnectedBoard();
  alazar_board_detected_ = detection.supported;
  alazar_detection_detail_ = QString::fromStdString(detection.detail);
  if (!detection.supported) {
    updateDigitizerBoardDisplay();
    return false;
  }

  const auto detected_profile = QString::fromStdString(detection.profile_id);
  const auto profile_changed = board_profile_id_ != detected_profile;
  const auto preferred_rate = sample_rate_->currentData().toDouble();
  const auto preferred_range = input_range_->currentData().toDouble();
  const auto preferred_impedance = impedance_->count() > 0
      ? impedance_->currentData().toUInt()
      : 0U;
  const auto preferred_record_samples = static_cast<std::uint32_t>(sample_point_->value());

  board_profile_id_ = detected_profile;
  populateDigitizerCapabilities(board_profile_id_, preferred_rate, preferred_range,
                                preferred_impedance);
  static_cast<RecordLengthSpinBox*>(sample_point_)->setSupportedValue(preferred_record_samples);
  updateDigitizerBoardDisplay();

  if (profile_changed && !loading_controls_) {
    markRestartDirty();
  }
  if (profile_changed || loading_controls_) {
    appendLog("INFO", "Digitizer", alazar_detection_detail_);
  }
  return true;
}

void MainWindow::updateDigitizerBoardDisplay() {
  if (board_model_ == nullptr || board_address_ == nullptr) {
    return;
  }
  const auto source = static_cast<AcquisitionSource>(acquisition_source_->currentData().toInt());
  const auto* capabilities = findDigitizerBoardCapabilities(board_profile_id_.toStdString());
  const auto model = capabilities == nullptr
      ? QString("Unknown profile")
      : QString::fromStdString(capabilities->display_name);

  if (source == AcquisitionSource::Alazar) {
    if (alazar_board_detected_) {
      board_model_->setText(QString("%1 | auto-detected").arg(model));
      board_model_->setProperty("statusKind", "ready");
      board_address_->setText(QString("System 1 / Board 1 | %1").arg(model));
      board_address_->setProperty("statusKind", "ready");
    } else {
      board_model_->setText("No supported board detected");
      board_model_->setProperty("statusKind", "error");
      board_address_->setText(alazar_detection_detail_.isEmpty()
          ? "System 1 / Board 1 | detection pending"
          : alazar_detection_detail_);
      board_address_->setProperty("statusKind", "error");
    }
  } else if (source == AcquisitionSource::Replay) {
    board_model_->setText(QString("%1 | recording profile").arg(model));
    board_model_->setProperty("statusKind", "neutral");
    board_address_->setText("Recorded DMA stream");
    board_address_->setProperty("statusKind", "neutral");
  } else {
    board_model_->setText(QString("%1 | simulator profile").arg(model));
    board_model_->setProperty("statusKind", "neutral");
    board_address_->setText("Generated signal batches");
    board_address_->setProperty("statusKind", "neutral");
  }
  repolish(board_model_);
  repolish(board_address_);
}

void MainWindow::updateDerivedAcquisitionLabels() {
  if (sample_point_ == nullptr || pre_trigger_ == nullptr || record_length_state_ == nullptr) {
    return;
  }
  const auto* capabilities = findDigitizerBoardCapabilities(
      board_profile_id_.toStdString());
  const auto sample_points = static_cast<std::uint32_t>(sample_point_->value());
  const QSignalBlocker pre_trigger_blocker(pre_trigger_);
  const auto maximum_pretrigger_from_record = sample_points > kAlazarMinimumPostTriggerSamples
      ? sample_points - kAlazarMinimumPostTriggerSamples
      : 0U;
  const auto maximum_pretrigger = capabilities == nullptr
      ? maximum_pretrigger_from_record
      : std::min(maximum_pretrigger_from_record,
                 capabilities->maximum_npt_pretrigger_samples);
  pre_trigger_->setMaximum(static_cast<int>(maximum_pretrigger));
  if (pre_trigger_->value() > sample_point_->value()) {
    pre_trigger_->setValue(sample_point_->value());
  }
  const auto post_trigger = sample_point_->value() - pre_trigger_->value();
  post_trigger_->setText(QString("%1 samples | derived").arg(post_trigger));

  const bool record_valid = capabilities != nullptr &&
      supportsRecordLength(*capabilities, sample_points);
  const double sample_rate_hz = sample_rate_->currentData().toDouble();
  const double record_duration_us = sample_rate_hz > 0.0
      ? static_cast<double>(sample_points) * 1.0e6 / sample_rate_hz
      : 0.0;
  if (!record_valid) {
    record_length_state_->setText(capabilities == nullptr
        ? "ATS ERROR | unknown board profile"
        : QString("ATS ERROR | min %1 | multiple of %2")
              .arg(capabilities->minimum_record_samples)
              .arg(capabilities->record_resolution_samples));
    setStyledProperty(record_length_state_, "statusKind", "error");
  } else {
    record_length_state_->setText(
        QString("ATS VALID | %1 us record").arg(record_duration_us, 0, 'f', 3));
    setStyledProperty(record_length_state_, "statusKind", "ready");
  }
  const auto a_scans = records_per_buffer_->value();
  if (selected_a_scan_ != nullptr) {
    selected_a_scan_->setMaximum(std::max(0, a_scans - 1));
  }
  if (selected_a_scan_slider_ != nullptr) {
    selected_a_scan_slider_->setMaximum(std::max(0, a_scans - 1));
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
  const auto point_rate_hz = kMcuWaveformPointRateHz;
  mcu_point_rate_->setText(QString("%1 kHz | firmware TIM6").arg(point_rate_hz / 1000.0, 0, 'g', 6));
  const auto waveform_source = static_cast<McuWaveformSource>(
      mcu_waveform_source_->currentData().toInt());
  const bool legacy_file = waveform_source == McuWaveformSource::LegacyXymFile;
  double mcu_frame_time_ms = 0.0;
  if (legacy_file) {
    if (runtime_status_.mcu_waveform_loaded && !restart_dirty_) {
      mcu_frame_time_ms = runtime_status_.mcu_frame_time_ms;
      mcu_frame_time_->setText(QString("%1 ms | %2 file points")
                                   .arg(mcu_frame_time_ms, 0, 'f', 3)
                                   .arg(runtime_status_.mcu_waveform_points));
    } else {
      const auto file_name = QFileInfo(mcu_waveform_file_->text()).fileName();
      mcu_frame_time_->setText(file_name.isEmpty()
          ? "Select an X/Y/M file"
          : QString("%1 | cycle derived during upload").arg(file_name));
    }
  } else {
    mcu_frame_time_ms = static_cast<double>(frame_points) * 1000.0 / point_rate_hz;
    mcu_frame_time_->setText(QString("%1 ms | %2 generated points")
                                 .arg(mcu_frame_time_ms, 0, 'f', 3)
                                 .arg(frame_points));
  }
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
    setStyledProperty(frame_sync_state_, "statusKind", synchronized ? "ready" : "warn");
  } else {
    frame_sync_state_->setText(legacy_file && !runtime_status_.mcu_waveform_loaded
        ? "Upload waveform to compare DMA / MCU timing"
        : "Waiting for DMA timing");
    setStyledProperty(frame_sync_state_, "statusKind", "neutral");
  }
}

void MainWindow::loadConfigToControls(const SystemConfig& config, bool mark_pending) {
  loading_controls_ = true;
  auto controls_config = config;
  const bool is_jetson =
      platform_name_.compare(QStringLiteral("Jetson"), Qt::CaseInsensitive) == 0;
  if (is_jetson) {
    controls_config.processing.fft_backend = FftBackendKind::Cuda;
  }
  controls_config_ = std::move(controls_config);
  if (!mark_pending) {
    config_ = controls_config_;
  }
  const auto& loaded = controls_config_;
  const QSignalBlocker source_blocker(acquisition_source_);
  profile_combo_->clear();
  profile_combo_->addItem(QString::fromStdString(loaded.profile.name));
  const auto source_index = acquisition_source_->findData(
      static_cast<int>(loaded.runtime.acquisition_source));
  acquisition_source_->setCurrentIndex(source_index >= 0 ? source_index : 0);
  replay_file_->setText(QString::fromStdString(loaded.runtime.replay_file));
  loaded_replay_setup_path_ = loaded.runtime.acquisition_source == AcquisitionSource::Replay
      ? normalizedReplayPath(replay_file_->text())
      : QString{};
  replay_loop_->setChecked(loaded.runtime.replay_loop);
  board_profile_id_ = QString::fromStdString(loaded.digitizer.board_profile);
  if (findDigitizerBoardCapabilities(board_profile_id_.toStdString()) == nullptr &&
      !digitizerBoardCapabilities().empty()) {
    board_profile_id_ = QString::fromStdString(digitizerBoardCapabilities().front().profile_id);
  }
  populateDigitizerCapabilities(board_profile_id_, loaded.digitizer.sample_rate_hz,
                                loaded.digitizer.input_range_volts, loaded.digitizer.impedance_ohms);
  digitizer_channel_->setCurrentIndex(loaded.digitizer.channel == DigitizerChannel::A ? 0 : 1);
  static_cast<RecordLengthSpinBox*>(sample_point_)->setSupportedValue(loaded.digitizer.sample_point);
  records_per_buffer_->setValue(static_cast<int>(loaded.digitizer.records_per_buffer));
  dma_buffer_count_->setValue(static_cast<int>(loaded.digitizer.dma_buffer_count));
  coupling_->setCurrentIndex(0);
  trigger_slope_->setCurrentIndex(loaded.digitizer.trigger_slope == TriggerSlope::Rising ? 0 : 1);
  trigger_delay_->setValue(static_cast<int>(loaded.digitizer.trigger_delay_samples));
  pre_trigger_->setValue(static_cast<int>(loaded.digitizer.pre_trigger_samples));
  sweep_bandwidth_->setValue(loaded.laser.sweep_bandwidth_hz);
  sweep_rate_->setValue(loaded.laser.sweep_rate_hz);
  edfa_mode_->setCurrentIndex(static_cast<int>(loaded.edfa.mode));
  refreshEdfaSerialPorts(QString::fromStdString(loaded.edfa.port));
  edfa_control_mode_->setCurrentIndex(0);
  edfa_setpoint_->setValue(loaded.edfa.output_setpoint.value);
  edfa_warmup_->setValue(static_cast<int>(loaded.edfa.warmup_delay_ms));
  x_start_->setValue(loaded.scan.x_start_deg);
  x_end_->setValue(loaded.scan.x_end_deg);
  y_start_->setValue(loaded.scan.y_start_deg);
  y_end_->setValue(loaded.scan.y_end_deg);
  y_lines_->setValue(static_cast<int>(loaded.scan.y_line_count));
  bidirectional_->setChecked(loaded.scan.bidirectional);
  mcu_enabled_->setChecked(loaded.mcu.enabled);
  refreshMcuSerialPorts(QString::fromStdString(loaded.mcu.port));
  const auto waveform_source_index = mcu_waveform_source_->findData(
      static_cast<int>(loaded.mcu.waveform_source));
  mcu_waveform_source_->setCurrentIndex(waveform_source_index >= 0 ? waveform_source_index : 0);
  mcu_waveform_file_->setText(QString::fromStdString(loaded.mcu.waveform_file));
  mcu_trigger_shift_->setValue(loaded.scan.trigger_shift_samples);
  updateMcuWaveformControls();
  fft_backend_->setCurrentIndex(
      is_jetson ? 0 : (loaded.processing.fft_backend == FftBackendKind::Fftw ? 0 : 1));
  window_function_->setCurrentIndex(static_cast<int>(loaded.chirp_segmentation.window));
  fft_length_->setValue(static_cast<int>(loaded.chirp_segmentation.segment_fft_length));
  updatePeakBinLimits();
  dc_removal_->setChecked(loaded.processing.dc_removal);
  peak_threshold_->setValue(loaded.processing.peak_threshold_db);
  peak_start_->setValue(static_cast<int>(loaded.processing.peak_search_start_bin));
  peak_end_->setValue(static_cast<int>(loaded.processing.peak_search_end_bin));
  period_start_->setValue(loaded.chirp_segmentation.trigger_to_period_offset);
  up_start_->setValue(static_cast<int>(loaded.chirp_segmentation.up_segment.start_sample));
  up_length_->setValue(static_cast<int>(loaded.chirp_segmentation.up_segment.length()));
  down_start_->setValue(static_cast<int>(loaded.chirp_segmentation.down_segment.start_sample));
  down_length_->setValue(static_cast<int>(loaded.chirp_segmentation.down_segment.length()));
  guard_samples_->setValue(static_cast<int>(loaded.chirp_segmentation.guard_samples));
  raw_enabled_->setChecked(loaded.storage.raw_enabled);
  processed_enabled_->setChecked(loaded.storage.processed_enabled);
  output_directory_->setText(QString::fromStdString(loaded.storage.output_directory));
  storage_queue_->setValue(static_cast<int>(loaded.storage.queue_capacity));
  split_size_->setValue(loaded.storage.split_file_size_gb);
  udp_enabled_->setChecked(loaded.udp.enabled);
  udp_ip_->setText(QString::fromStdString(loaded.udp.target_ip));
  udp_port_->setValue(loaded.udp.target_port);
  udp_points_->setValue(static_cast<int>(loaded.udp.packet_point_count));
  udp_version_->setCurrentIndex(std::max(0, udp_version_->findData(loaded.udp.packet_format_version)));
  udp_queue_->setValue(static_cast<int>(loaded.udp.queue_capacity));
  udp_policy_->setCurrentIndex(loaded.udp.backpressure_policy == UdpBackpressurePolicy::LatestFrame ? 0
      : loaded.udp.backpressure_policy == UdpBackpressurePolicy::PreserveFrames ? 1 : 2);
  if (loaded.runtime.acquisition_source == AcquisitionSource::Alazar) {
    refreshDigitizerBoardModel();
  }
  updateDigitizerBoardDisplay();
  updateRuntimeSourceControls();
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
  segmentation_plot_->setSegments(segmentRangeFromStartAndLength(
                                       static_cast<std::uint32_t>(up_start_->value()),
                                       static_cast<std::uint32_t>(up_length_->value())),
                                   segmentRangeFromStartAndLength(
                                       static_cast<std::uint32_t>(down_start_->value()),
                                       static_cast<std::uint32_t>(down_length_->value())),
                                   static_cast<std::uint32_t>(guard_samples_->value()));
  digitizer_lock_state_->setText(mark_pending ? "APPLY REQUIRED | reconnect before START"
                                              : "READY | board settings applied");
  digitizer_lock_state_->setProperty("statusKind", mark_pending ? "warn" : "ready");
  repolish(digitizer_lock_state_);
  updateEdfaStatusDisplay();
  validateControls();
}

void MainWindow::refreshEdfaSerialPorts(QString preferred_port) {
  if (edfa_port_ == nullptr) {
    return;
  }
  populateSerialPortCombo(edfa_port_, availableSerialPorts(), std::move(preferred_port),
                          platform_name_, SerialPortRole::Edfa);
}

void MainWindow::refreshMcuSerialPorts(QString preferred_port) {
  if (mcu_port_ == nullptr) {
    return;
  }
  populateSerialPortCombo(mcu_port_, availableSerialPorts(), std::move(preferred_port),
                          platform_name_, SerialPortRole::Mcu);
}

void MainWindow::updateEdfaStatusDisplay() {
  if (edfa_connection_status_ == nullptr) {
    return;
  }
  auto setStatus = [](QLabel* label, const QString& text, const char* kind) {
    label->setText(text);
    setStyledProperty(label, "statusKind", kind);
  };
  const auto mode = static_cast<EdfaMode>(edfa_mode_->currentIndex());
  if (mode == EdfaMode::None) {
    setStatus(edfa_connection_status_, "Not used | bypass", "neutral");
    setStatus(edfa_activation_status_, "OFF | bypass", "neutral");
    setStatus(edfa_target_status_, "Not applicable", "neutral");
    setStatus(edfa_input_status_, "Not applicable", "neutral");
    setStatus(edfa_output_status_, "Not applicable", "neutral");
    setStatus(edfa_current_status_, "Not applicable", "neutral");
    return;
  }
  if (mode == EdfaMode::Manual) {
    setStatus(edfa_connection_status_, "Manual operator control", "neutral");
    setStatus(edfa_activation_status_, "Not controlled by software", "neutral");
    setStatus(edfa_target_status_, "Session metadata only", "neutral");
    setStatus(edfa_input_status_, "No serial readback", "neutral");
    setStatus(edfa_output_status_, "No serial readback", "neutral");
    setStatus(edfa_current_status_, "No serial readback", "neutral");
    return;
  }

  const auto selected_port = edfa_port_->currentData().toString();
  const auto connected_port = runtime_status_.edfa_port.isEmpty()
      ? selected_port : runtime_status_.edfa_port;
  setStatus(edfa_connection_status_, runtime_status_.edfa_connected
                ? QString("Connected | %1 | %2")
                      .arg(connected_port, runtime_status_.edfa_device_name)
                : QString("Disconnected | %1")
                      .arg(selected_port.isEmpty() ? "no port" : selected_port),
             runtime_status_.edfa_connected ? "ready" : "warn");
  setStatus(edfa_activation_status_, runtime_status_.edfa_output_enabled
                ? "ON | device confirmed"
                : runtime_status_.edfa_connected ? "OFF | device confirmed" : "Unknown",
            runtime_status_.edfa_output_enabled ? "warn" :
                runtime_status_.edfa_connected ? "ready" : "neutral");
  if (!runtime_status_.edfa_telemetry_valid) {
    setStatus(edfa_target_status_, "Waiting for device readback", "neutral");
    setStatus(edfa_input_status_, "Waiting for device readback", "neutral");
    setStatus(edfa_output_status_, "Waiting for device readback", "neutral");
    setStatus(edfa_current_status_, "Waiting for device readback", "neutral");
    return;
  }
  setStatus(edfa_target_status_,
            QString("%1 dBm | %2")
                .arg(runtime_status_.edfa_target_dbm, 0, 'f', 2)
                .arg(runtime_status_.edfa_control_mode), "ready");
  setStatus(edfa_input_status_, QString("%1 dBm").arg(runtime_status_.edfa_input_dbm, 0, 'f', 2), "ready");
  setStatus(edfa_output_status_, QString("%1 dBm").arg(runtime_status_.edfa_output_dbm, 0, 'f', 2),
            runtime_status_.edfa_output_enabled ? "ready" : "neutral");
  setStatus(edfa_current_status_, QString("%1 mA").arg(runtime_status_.edfa_current_ma, 0, 'f', 0), "ready");
}

void MainWindow::updateRuntimeSourceControls() {
  const auto source = static_cast<AcquisitionSource>(acquisition_source_->currentData().toInt());
  const bool replay = source == AcquisitionSource::Replay;
  const bool editable = !runtime_status_.running && !mcu_uploading_;
  replay_file_->setEnabled(replay && editable);
  replay_browse_->setEnabled(replay && editable);
  replay_loop_->setEnabled(replay && editable);
  updateDigitizerBoardDisplay();
  updateMcuWaveformControls();
}

void MainWindow::updateMcuWaveformControls() {
  if (mcu_waveform_source_ == nullptr || mcu_waveform_file_ == nullptr ||
      mcu_waveform_browse_ == nullptr) {
    return;
  }
  const auto source = static_cast<McuWaveformSource>(mcu_waveform_source_->currentData().toInt());
  const bool file_mode = source == McuWaveformSource::LegacyXymFile;
  const bool editable = !runtime_status_.running && !mcu_uploading_;
  mcu_waveform_file_->setEnabled(file_mode && editable);
  mcu_waveform_browse_->setEnabled(file_mode && editable);
  bidirectional_->setText(file_mode ? "Bidirectional vector scan" : "Bidirectional raster");
  bidirectional_->setEnabled(editable);
  bidirectional_->setToolTip(file_mode
      ? "When enabled, odd B-scan lines are placed in reverse X order. "
        "When disabled, every line keeps acquisition order."
      : "Reverse the generated fast-axis command on alternating scan lines.");
  upload_waveform_button_->setEnabled(editable && runtime_status_.connected &&
                                      mcu_enabled_->isChecked() && !restart_dirty_);
  mcu_waveform_file_->setToolTip(file_mode
      ? "Header: sps <rate>. Remaining rows: normalized X Y and marker M."
      : "Generated raster contains one command point per A-scan across all B-scans.");
}

void MainWindow::updateMcuUploadProgress(McuUploadProgress progress) {
  mcu_upload_progress_state_ = std::move(progress);
  const auto stage = mcu_upload_progress_state_.stage;
  const bool active = stage == McuUploadStage::Preparing || stage == McuUploadStage::Clearing ||
      stage == McuUploadStage::Sending || stage == McuUploadStage::Verifying;
  if (active && !mcu_upload_elapsed_timer_.isValid()) {
    mcu_upload_elapsed_timer_.start();
  }
  mcu_uploading_ = active;

  const auto completed = mcu_upload_progress_state_.completed_points;
  const auto total = mcu_upload_progress_state_.total_points;
  const auto detail = QString::fromStdString(mcu_upload_progress_state_.detail);
  const auto points = [](std::uint32_t value) {
    return QLocale().toString(static_cast<qulonglong>(value));
  };

  switch (stage) {
    case McuUploadStage::Idle:
      mcu_upload_progress_->setRange(0, 100);
      mcu_upload_progress_->setValue(0);
      mcu_upload_progress_->setFormat("Idle");
      break;
    case McuUploadStage::Preparing:
      mcu_upload_progress_->setRange(0, 0);
      mcu_upload_progress_->setFormat("Preparing waveform");
      mcu_waveform_state_->setText(detail.isEmpty() ? "Preparing waveform" : detail);
      setStyledProperty(mcu_waveform_state_, "statusKind", "neutral");
      break;
    case McuUploadStage::Clearing:
      mcu_upload_progress_->setRange(0, 0);
      mcu_upload_progress_->setFormat("Clearing MCU memory");
      mcu_waveform_state_->setText("Clearing previous waveform | waiting for ACK:CLR");
      setStyledProperty(mcu_waveform_state_, "statusKind", "warn");
      break;
    case McuUploadStage::Sending: {
      mcu_upload_progress_->setRange(0, std::max(1, static_cast<int>(total)));
      mcu_upload_progress_->setValue(static_cast<int>(completed));
      mcu_upload_progress_->setFormat("Sending %p%");
      const auto remaining = total > completed ? total - completed : 0U;
      QString eta = "ETA calculating";
      if (completed > 0U && mcu_upload_elapsed_timer_.isValid() &&
          mcu_upload_elapsed_timer_.elapsed() >= 250) {
        const auto elapsed_seconds = static_cast<double>(mcu_upload_elapsed_timer_.elapsed()) / 1000.0;
        const auto points_per_second = static_cast<double>(completed) / elapsed_seconds;
        if (points_per_second > 0.0) {
          eta = QString("ETA %1 s").arg(static_cast<double>(remaining) / points_per_second, 0, 'f', 1);
        }
      }
      mcu_waveform_state_->setText(QString("Sending | %1 / %2 points | %3 remaining | %4")
                                       .arg(points(completed), points(total), points(remaining), eta));
      setStyledProperty(mcu_waveform_state_, "statusKind", "warn");
      break;
    }
    case McuUploadStage::Verifying:
      mcu_upload_progress_->setRange(0, std::max(1, static_cast<int>(total)));
      mcu_upload_progress_->setValue(static_cast<int>(total));
      mcu_upload_progress_->setFormat("Sent 100% | Verifying");
      mcu_waveform_state_->setText("Verifying | waiting for ACK:LOAD_DONE and point-count match");
      setStyledProperty(mcu_waveform_state_, "statusKind", "warn");
      break;
    case McuUploadStage::Complete:
      mcu_upload_progress_->setRange(0, std::max(100, static_cast<int>(total)));
      mcu_upload_progress_->setValue(std::max(100, static_cast<int>(total)));
      mcu_upload_progress_->setFormat("Upload complete");
      mcu_waveform_state_->setText(detail.isEmpty() ? "Ready | waveform verified" :
                                                    QString("Ready | %1").arg(detail));
      setStyledProperty(mcu_waveform_state_, "statusKind", "ready");
      mcu_upload_elapsed_timer_.invalidate();
      break;
    case McuUploadStage::Failed:
      mcu_upload_progress_->setRange(0, std::max(1, static_cast<int>(total)));
      mcu_upload_progress_->setValue(static_cast<int>(std::min(completed, total)));
      mcu_upload_progress_->setFormat("Upload failed");
      mcu_waveform_state_->setText(detail.isEmpty() ? "Upload failed" : QString("Error | %1").arg(detail));
      setStyledProperty(mcu_waveform_state_, "statusKind", "error");
      mcu_upload_elapsed_timer_.invalidate();
      break;
  }
  updateControlAvailability();
  validateControls();
}

void MainWindow::updateMcuStatusDisplay() {
  if (mcu_uploading_) {
    return;
  }
  if (mcu_upload_progress_state_.stage == McuUploadStage::Failed && runtime_status_.connected) {
    return;
  }
  if (restart_dirty_) {
    setStatusText(mcu_waveform_state_, "Setup changed | Apply Setup, then upload waveform", false);
    return;
  }
  if (runtime_status_.mcu_bypassed) {
    setStatusText(mcu_waveform_state_, "MCU bypass active", true, true);
    return;
  }
  if (runtime_status_.mcu_scan_active) {
    setStatusText(mcu_waveform_state_,
                  QString("Running | %1 points | %2")
                      .arg(runtime_status_.mcu_waveform_points)
                      .arg(runtime_status_.mcu_last_ack),
                  true);
    return;
  }
  if (runtime_status_.mcu_waveform_loaded) {
    setStatusText(mcu_waveform_state_,
                  QString("Ready | %1 points | %2 ms | %3")
                      .arg(runtime_status_.mcu_waveform_points)
                      .arg(runtime_status_.mcu_frame_time_ms, 0, 'f', 3)
                      .arg(runtime_status_.mcu_last_ack),
                  runtime_status_.mcu_ready);
    return;
  }
  const auto detail = runtime_status_.mcu_detail.isEmpty()
      ? (runtime_status_.connected ? QString("Upload required") : QString("MCU disconnected"))
      : runtime_status_.mcu_detail;
  setStatusText(mcu_waveform_state_, detail, false);
  if ((!runtime_status_.connected || mcu_upload_progress_state_.stage == McuUploadStage::Complete) &&
      mcu_upload_progress_state_.stage != McuUploadStage::Failed) {
    mcu_upload_progress_state_ = {};
    mcu_upload_progress_->setRange(0, 100);
    mcu_upload_progress_->setValue(0);
    mcu_upload_progress_->setFormat("Idle");
  }
}

void MainWindow::updateControlAvailability() {
  const bool editable = !runtime_status_.running && !mcu_uploading_;
  connect_button_->setEnabled(editable);
  load_button_->setEnabled(editable);
  save_button_->setEnabled(editable);
  profile_combo_->setEnabled(editable);
  for (auto* control : restart_required_controls_) {
    control->setEnabled(editable);
  }
  updateRuntimeSourceControls();
}

void MainWindow::updatePeakBinLimits() {
  if (fft_length_ == nullptr || peak_start_ == nullptr || peak_end_ == nullptr) {
    return;
  }
  const int last_usable_bin = std::max(1, fft_length_->value() / 2 - 1);
  peak_end_->setMaximum(last_usable_bin);
  peak_start_->setMaximum(std::max(0, last_usable_bin - 1));
}

void MainWindow::updateLivePlotSubscription() {
  if (controller_ == nullptr || navigation_ == nullptr || live_tabs_ == nullptr) {
    return;
  }
  const int plot_index = !freeze_live_ && navigation_->currentRow() == kLivePageIndex
      ? live_tabs_->currentIndex()
      : -1;
  time_plot_->resetDisplayMetrics();
  fft_plot_->resetDisplayMetrics();
  static_cast<void>(controller_->takeUiDispatchMetrics());
  if (live_display_diagnostics_ != nullptr) {
    if (plot_index == 0 || plot_index == 1) {
      live_display_diagnostics_->setText(
          "Display | collecting 1 second diagnostic window");
    } else if (plot_index >= 2) {
      live_display_diagnostics_->setText(
          "Display | selected A-scan snapshots are paused outside Time Domain / FFT");
    } else {
      live_display_diagnostics_->setText("Display | inactive");
    }
  }
  controller_->setLivePlotIndex(plot_index);
}

void MainWindow::updateLiveDisplayDiagnostics() {
  if (live_display_diagnostics_ == nullptr || time_plot_ == nullptr || fft_plot_ == nullptr ||
      live_tabs_ == nullptr) {
    return;
  }

  const auto time_metrics = time_plot_->takeDisplayMetrics();
  const auto fft_metrics = fft_plot_->takeDisplayMetrics();
  const auto dispatch_metrics = controller_->takeUiDispatchMetrics();
  const int plot_index = navigation_ != nullptr && navigation_->currentRow() == kLivePageIndex &&
          !freeze_live_
      ? live_tabs_->currentIndex()
      : -1;
  if (!runtime_status_.running) {
    live_display_diagnostics_->setText("Display | acquisition stopped");
    return;
  }
  if (freeze_live_) {
    live_display_diagnostics_->setText("Display | frozen");
    return;
  }
  if (plot_index != 0 && plot_index != 1) {
    live_display_diagnostics_->setText(
        "Display | selected A-scan snapshots are paused outside Time Domain / FFT");
    return;
  }

  const auto& metrics = plot_index == 0 ? time_metrics : fft_metrics;
  if (metrics.delivery_count == 0U) {
    live_display_diagnostics_->setText("Display | no selected plot snapshot delivered in the last second");
    return;
  }

  const double dma_rate_hz = runtime_status_.dma_bscan_rate_hz > 0.0
      ? runtime_status_.dma_bscan_rate_hz
      : metrics.observed_source_hz;
  const double omitted_per_second = metrics.interval_seconds > 0.0
      ? static_cast<double>(metrics.dma_sequences_not_delivered) / metrics.interval_seconds
      : 0.0;
  const double merged_per_second = metrics.interval_seconds > 0.0
      ? static_cast<double>(metrics.gui_updates_merged) / metrics.interval_seconds
      : 0.0;
  const auto snapshot_published = plot_index == 0
      ? dispatch_metrics.waveform_published
      : dispatch_metrics.fft_published;
  const auto controller_coalesced = plot_index == 0
      ? dispatch_metrics.waveform_coalesced
      : dispatch_metrics.fft_coalesced;
  const double snapshot_hz = metrics.interval_seconds > 0.0
      ? static_cast<double>(snapshot_published) / metrics.interval_seconds
      : 0.0;
  const double controller_merged_per_second = metrics.interval_seconds > 0.0
      ? static_cast<double>(controller_coalesced) / metrics.interval_seconds
      : 0.0;
  live_display_diagnostics_->setText(
      QString("Display Hz | DMA %1 | snapshot %2 | GUI %3 | paint %4\n"
              "Coalesced/s | before GUI %5 | before paint %6 | not shown %7 | "
              "step max %8 | set p95 %9 ms | paint p95/max %10/%11 ms")
          .arg(dma_rate_hz, 0, 'f', 1)
          .arg(snapshot_hz, 0, 'f', 1)
          .arg(metrics.delivery_hz, 0, 'f', 1)
          .arg(metrics.paint_hz, 0, 'f', 1)
          .arg(controller_merged_per_second, 0, 'f', 1)
          .arg(merged_per_second, 0, 'f', 1)
          .arg(omitted_per_second, 0, 'f', 1)
          .arg(metrics.maximum_dma_step)
          .arg(metrics.set_series_p95_ms, 0, 'f', 2)
          .arg(metrics.paint_p95_ms, 0, 'f', 2)
          .arg(metrics.paint_max_ms, 0, 'f', 2));
}

bool MainWindow::isLivePlotActive(int plot_index) const {
  return !freeze_live_ && navigation_ != nullptr && navigation_->currentRow() == kLivePageIndex &&
      live_tabs_ != nullptr && live_tabs_->currentIndex() == plot_index;
}

void MainWindow::updateSelectedAScanStatus(std::uint32_t record_index,
                                           std::uint32_t records_in_buffer,
                                           std::uint64_t dma_sequence) {
  selected_a_scan_status_->setText(QString("A-scan %1 / %2 | latest DMA %3")
                                       .arg(record_index)
                                       .arg(records_in_buffer == 0U ? 0U : records_in_buffer - 1U)
                                       .arg(dma_sequence));
  if (selected_a_scan_status_->property("statusKind").toString() != "ready") {
    selected_a_scan_status_->setProperty("statusKind", "ready");
    repolish(selected_a_scan_status_);
  }
}

void MainWindow::updateProcessingTelemetryLabels() {
  if (batch_workload_ == nullptr || batch_latency_ == nullptr ||
      batch_percentiles_ == nullptr || batch_deadline_ == nullptr) {
    return;
  }
  batch_workload_->setText(QString("%1 samples x %2 records\n%3 FFTs x %4")
                               .arg(sample_point_->value())
                               .arg(records_per_buffer_->value())
                               .arg(records_per_buffer_->value() * 2)
                               .arg(fft_length_->value()));
  batch_latency_->setText(QString("%1 ms last | %2 ms mean\n%3 ms ownership | %4 ms signal")
                              .arg(runtime_status_.processing_batch_latency_ms, 0, 'f', 3)
                              .arg(runtime_status_.processing_batch_average_ms, 0, 'f', 3)
                              .arg(runtime_status_.processing_copy_latency_ms, 0, 'f', 3)
                              .arg(runtime_status_.processing_signal_latency_ms, 0, 'f', 3));
  batch_percentiles_->setText(QString("p50 %1 | p95 %2\np99 %3 | max %4 ms")
                                  .arg(runtime_status_.processing_batch_p50_ms, 0, 'f', 3)
                                  .arg(runtime_status_.processing_batch_p95_ms, 0, 'f', 3)
                                  .arg(runtime_status_.processing_batch_p99_ms, 0, 'f', 3)
                                  .arg(runtime_status_.processing_batch_max_ms, 0, 'f', 3));
  const auto deadline_margin_ms = runtime_status_.processing_deadline_ms -
      runtime_status_.processing_batch_latency_ms;
  batch_deadline_->setText(QString("%1 ms margin | %2 misses")
                               .arg(deadline_margin_ms, 0, 'f', 3)
                               .arg(runtime_status_.processing_deadline_misses));
  setStyledProperty(batch_deadline_, "statusKind",
                    runtime_status_.processing_deadline_misses == 0U ? "ready" : "error");
}

void MainWindow::applyProfile() {
  if (mcu_uploading_) {
    statusBar()->showMessage("Apply Setup is locked until the MCU waveform upload finishes", 5000);
    return;
  }
  if (!validateControls(true)) {
    return;
  }
  const auto candidate = configFromControls();
  if (ConfigProfileCodec::toJsonSnapshot(candidate) == ConfigProfileCodec::toJsonSnapshot(config_)) {
    config_dirty_ = false;
    restart_dirty_ = false;
    validateControls();
    updateMcuWaveformControls();
    updateMcuStatusDisplay();
    statusBar()->showMessage("Apply Setup: no changes; current device state preserved", 5000);
    return;
  }
  controller_->applyConfig(candidate);
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
  auto loaded_config = result.config;
  loaded_config.profile.name = QFileInfo(path).completeBaseName().toStdString();
  loadConfigToControls(loaded_config, true);
  appendLog("INFO", "Configuration", QString("Loaded profile %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::loadReplaySetup(const QString& raw_path) {
  const auto absolute_raw_path = QFileInfo(raw_path).absoluteFilePath();
  if (!replayPathChanged(absolute_raw_path, loaded_replay_setup_path_)) {
    return;
  }

  const auto setup = loadRecordedSetup(absolute_raw_path);
  if (!setup.ok()) {
    const QSignalBlocker source_blocker(acquisition_source_);
    acquisition_source_->setCurrentIndex(
        acquisition_source_->findData(static_cast<int>(AcquisitionSource::Replay)));
    replay_file_->setText(absolute_raw_path);
    loaded_replay_setup_path_ = normalizedReplayPath(absolute_raw_path);
    updateRuntimeSourceControls();
    markRestartDirty();
    const auto warning_detail = setup.warnings.isEmpty()
        ? QString{}
        : setup.warnings.join(QStringLiteral("\n")) + QStringLiteral("\n");
    QMessageBox::warning(this, "Replay setup not found",
                         warning_detail + setup.error +
                             "\nThe current setup will be used and must match the RAW stream.");
    return;
  }

  auto recorded = setup.config;
  recorded.runtime.acquisition_source = AcquisitionSource::Replay;
  recorded.runtime.replay_file = absolute_raw_path.toStdString();
  recorded.runtime.replay_loop = false;
  recorded.mcu.enabled = false;
  recorded.edfa.mode = EdfaMode::None;
  recorded.edfa.required_before_start = false;
  recorded.storage.raw_enabled = false;
  recorded.storage.processed_enabled = false;
  recorded.udp.enabled = false;
  loadConfigToControls(recorded, true);
  for (const auto& warning : setup.warnings) {
    appendLog("WARNING", "Replay", warning);
  }
  const auto* capabilities = findDigitizerBoardCapabilities(recorded.digitizer.board_profile);
  const auto board_name = capabilities == nullptr
      ? QString::fromStdString(recorded.digitizer.board_profile)
      : QStringLiteral("%1 [%2]")
            .arg(QString::fromStdString(capabilities->display_name),
                 QString::fromStdString(capabilities->profile_id));
  appendLog("INFO", "Replay",
            QString("Restored recorded setup from %1 | board %2 | hardware outputs disabled")
                .arg(QFileInfo(setup.setup_source).fileName(), board_name));
  statusBar()->showMessage(
      setup.used_json_fallback
          ? "Recorded setup restored from JSON fallback; review the Replay warnings before applying."
          : "Recorded setup restored. Review it, then Apply Setup to replay.",
      8000);
}

void MainWindow::saveProfile() {
  if (!validateControls(true)) {
    return;
  }
  const auto path = QFileDialog::getSaveFileName(this, "Save FMCW LiDAR profile", "profile.yaml", "YAML profile (*.yaml)");
  if (path.isEmpty()) {
    return;
  }
  auto saved_config = configFromControls();
  saved_config.profile.name = QFileInfo(path).completeBaseName().toStdString();
  std::string error;
  if (!ConfigProfileCodec::save(fileSystemPath(path), saved_config, error)) {
    QMessageBox::critical(this, "Profile save failed", QString::fromStdString(error));
    return;
  }
  profile_combo_->clear();
  profile_combo_->addItem(QString::fromStdString(saved_config.profile.name));
  controls_config_.profile.name = saved_config.profile.name;
  if (!config_dirty_) {
    config_.profile.name = saved_config.profile.name;
  }
  appendLog("INFO", "Configuration", QString("Saved profile %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::updateStatus(RuntimeStatus status) {
  const bool control_state_changed =
      runtime_status_.state != status.state ||
      runtime_status_.configured != status.configured ||
      runtime_status_.connected != status.connected ||
      runtime_status_.running != status.running ||
      runtime_status_.recording != status.recording ||
      runtime_status_.config_revision != status.config_revision ||
      runtime_status_.processing_revision != status.processing_revision ||
      runtime_status_.source_name != status.source_name;
  runtime_status_ = std::move(status);
  const bool stopping = runtime_status_.state == OperationState::Stopping;
  const auto state = QString::fromStdString(toString(runtime_status_.state)).toUpper();
  const auto source_name = runtime_status_.source_name.isEmpty()
      ? QString::fromStdString(toString(config_.runtime.acquisition_source)).toUpper()
      : runtime_status_.source_name.toUpper();
  runtime_source_badge_->setText(QString("%1  |  %2").arg(platform_name_.toUpper(), source_name));
  if (stopping) {
    if (!stop_stage_elapsed_timer_.isValid() ||
        displayed_stop_stage_ != runtime_status_.active_operation) {
      displayed_stop_stage_ = runtime_status_.active_operation;
      stop_stage_elapsed_timer_.restart();
    }
    if (!stop_stage_timer_->isActive()) {
      stop_stage_timer_->start();
    }
    updateStopStageDisplay();
  } else {
    stop_stage_timer_->stop();
    stop_stage_elapsed_timer_.invalidate();
    displayed_stop_stage_.clear();
    runtime_state_label_->setText(state);
    setStyledProperty(runtime_state_label_, "statusKind",
                      runtime_status_.state == OperationState::Error ? "error"
                          : runtime_status_.running || runtime_status_.connected ? "ready" : "neutral");
    statusBar()->showMessage(QString("%1 | Up-chirp trigger | Full-period DMA batch").arg(source_name), 0);
  }
  connect_button_->setText(runtime_status_.connected ? "Disconnect" : "Connect");
  start_stop_button_->setText(stopping ? "STOPPING..." : runtime_status_.running ? "STOP" : "START");
  setStyledProperty(start_stop_button_, "runState",
                    stopping ? "stopping" : runtime_status_.running ? "stop" : "start");
  if (control_state_changed) {
    updateControlAvailability();
  }
  if (runtime_status_.running) {
    digitizer_lock_state_->setText("LOCKED | press STOP before setup changes");
    setStyledProperty(digitizer_lock_state_, "statusKind", "warn");
  } else if (restart_dirty_) {
    digitizer_lock_state_->setText("APPLY REQUIRED | reconnect before START");
    setStyledProperty(digitizer_lock_state_, "statusKind", "warn");
  } else {
    digitizer_lock_state_->setText("READY | board settings applied");
    setStyledProperty(digitizer_lock_state_, "statusKind", "ready");
  }

  overview_digitizer_->setText(runtime_status_.digitizer_ready
      ? QString("READY\n%1").arg(source_name)
      : QString("NOT READY\n%1").arg(source_name));
  overview_edfa_->setText(runtime_status_.edfa_bypassed ? "BYPASS\nNo EDFA" :
                          runtime_status_.edfa_output_enabled ? "OUTPUT ON" :
                          runtime_status_.edfa_connected && runtime_status_.edfa_ready
                              ? "READY\nOutput off" : "NOT READY\nEDFA disconnected");
  overview_mcu_->setText(runtime_status_.mcu_bypassed ? "BYPASS\nMCU disabled" :
                         runtime_status_.mcu_waveform_loaded
                             ? QString("READY\n%1 points").arg(runtime_status_.mcu_waveform_points)
                             : "WAITING\nNo waveform");
  overview_processing_->setText(runtime_status_.backend_name.isEmpty() ? "NOT CONFIGURED" : runtime_status_.backend_name);
  const auto frame_point_count = derivedFramePointCount(config_);
  const auto generated_frames = frame_point_count == 0U
      ? 0U
      : runtime_status_.frames_processed / frame_point_count;
  if (!runtime_status_.running) {
    generated_frame_rate_timer_.invalidate();
    generated_frame_rate_count_ = generated_frames;
    generated_frame_rate_hz_ = 0.0;
  } else if (!generated_frame_rate_timer_.isValid() ||
             generated_frames < generated_frame_rate_count_) {
    generated_frame_rate_timer_.start();
    generated_frame_rate_count_ = generated_frames;
    generated_frame_rate_hz_ = 0.0;
  } else if (generated_frame_rate_timer_.elapsed() >= kFrameRateSampleIntervalMs) {
    const auto elapsed_ms = generated_frame_rate_timer_.elapsed();
    generated_frame_rate_hz_ = static_cast<double>(
        generated_frames - generated_frame_rate_count_) * 1000.0 /
        static_cast<double>(elapsed_ms);
    generated_frame_rate_count_ = generated_frames;
    generated_frame_rate_timer_.restart();
  }
  overview_frames_->setText(QString("%1 FPS\n%2 generated")
                                .arg(generated_frame_rate_hz_, 0, 'f', 2)
                                .arg(generated_frames));
  overview_queues_->setText(QString("Signal %1/%2\nRaw %3/%4 | Result %5/%6")
                                .arg(runtime_status_.processing_queue_size)
                                .arg(runtime_status_.processing_queue_capacity)
                                .arg(runtime_status_.raw_storage_queue_size)
                                .arg(runtime_status_.raw_storage_queue_capacity)
                                .arg(runtime_status_.processed_storage_queue_size)
                                .arg(runtime_status_.processed_storage_queue_capacity));
  const auto raw_megabytes = static_cast<double>(runtime_status_.raw_bytes_written) / 1.0e6;
  storage_status_->setText(runtime_status_.storage_stop_reason.isEmpty()
      ? QString("Raw %1/%2 (peak %3) | Result %4/%5 (peak %6)\n"
                "%7 blocks | %8 MB | %9 + %10 Mb/s")
            .arg(runtime_status_.raw_storage_queue_size)
            .arg(runtime_status_.raw_storage_queue_capacity)
            .arg(runtime_status_.raw_storage_queue_high_water)
            .arg(runtime_status_.processed_storage_queue_size)
            .arg(runtime_status_.processed_storage_queue_capacity)
            .arg(runtime_status_.processed_storage_queue_high_water)
            .arg(runtime_status_.raw_blocks_written)
            .arg(raw_megabytes, 0, 'f', 1)
            .arg(runtime_status_.raw_storage_throughput_mbps, 0, 'f', 1)
            .arg(runtime_status_.processed_storage_throughput_mbps, 0, 'f', 1)
      : runtime_status_.storage_stop_reason);
  setStyledProperty(storage_status_, "statusKind", runtime_status_.storage_stop_reason.isEmpty()
      ? (runtime_status_.raw_storage_queue_size > 0U ||
         runtime_status_.processed_storage_queue_size > 0U ? "warning" : "ready")
      : "error");
  overview_latency_->setText(QString("%1 ms last\n%2 misses")
                                 .arg(runtime_status_.processing_batch_latency_ms, 0, 'f', 3)
                                 .arg(runtime_status_.processing_deadline_misses));
  if (navigation_ != nullptr && navigation_->currentRow() == kProcessingPageIndex) {
    updateProcessingTelemetryLabels();
  }
  overview_recording_->setText(runtime_status_.recording
      ? QString("ACTIVE\n%1 frames").arg(runtime_status_.frames_written) : "OFF");
  if (runtime_status_.udp_running) {
    udp_indicator_->setText("UDP TX");
    setStyledProperty(udp_indicator_, "statusKind", "ready");
    udp_status_->setText(QString("Sending | %1 fps | %2 packets | queue %3 / %4 | %5 dropped")
                             .arg(runtime_status_.udp_send_fps, 0, 'f', 1)
                             .arg(runtime_status_.udp_packets_sent)
                             .arg(runtime_status_.udp_queue_size)
                             .arg(runtime_status_.udp_queue_capacity)
                             .arg(runtime_status_.udp_dropped_frames));
    setStyledProperty(udp_status_, "statusKind",
                      runtime_status_.udp_dropped_frames == 0U ? "ready" : "warn");
  } else if (udp_enabled_->isChecked()) {
    udp_indicator_->setText("UDP READY");
    setStyledProperty(udp_indicator_, "statusKind", "neutral");
    udp_status_->setText(runtime_status_.running ? "Sender stopped" : "Configured | starts with global START");
    setStyledProperty(udp_status_, "statusKind", runtime_status_.running ? "warn" : "neutral");
  } else {
    udp_indicator_->setText("UDP OFF");
    setStyledProperty(udp_indicator_, "statusKind", "neutral");
    udp_status_->setText("UDP off");
    setStyledProperty(udp_status_, "statusKind", "neutral");
  }
  overview_detail_->setText(QString("%1 | Config revision %2 | DMA drop %3 | Trigger miss %4 | %5")
                                .arg(state)
                                .arg(runtime_status_.config_revision)
                                .arg(runtime_status_.dma_buffer_drops)
                                .arg(runtime_status_.trigger_misses)
                                .arg(runtime_status_.detail));
  updateMcuStatusDisplay();
  if (navigation_ != nullptr && navigation_->currentRow() == kScanMcuPageIndex) {
    updateDerivedAcquisitionLabels();
  }
  updateEdfaStatusDisplay();
  edfa_output_button_->setText(runtime_status_.edfa_output_enabled ? "Disable Output" : "Enable Output");
  const bool edfa_disable_available = runtime_status_.connected && runtime_status_.edfa_output_enabled;
  const bool edfa_enable_available = runtime_status_.connected && edfa_mode_->currentIndex() == 2 &&
      runtime_status_.edfa_connected && runtime_status_.edfa_ready &&
      !runtime_status_.edfa_bypassed && !runtime_status_.running && !restart_dirty_;
  const bool edfa_output_available = edfa_disable_available || edfa_enable_available;
  if (edfa_output_button_->isEnabled() != edfa_output_available) {
    edfa_output_button_->setEnabled(edfa_output_available);
  }
  if (control_state_changed) {
    validateControls();
  }
}

void MainWindow::updateStopStageDisplay() {
  if (runtime_status_.state != OperationState::Stopping || runtime_state_label_ == nullptr) {
    return;
  }
  const auto stage = displayed_stop_stage_.isEmpty() ? QString("STOP") : displayed_stop_stage_;
  const auto elapsed_seconds = stop_stage_elapsed_timer_.isValid()
      ? static_cast<double>(stop_stage_elapsed_timer_.elapsed()) / 1000.0
      : 0.0;
  runtime_state_label_->setText(QString("STOPPING | %1").arg(stage));
  setStyledProperty(runtime_state_label_, "statusKind", "warn");
  statusBar()->showMessage(QString("%1 active | %2 s | %3")
                               .arg(stage)
                               .arg(elapsed_seconds, 0, 'f', 1)
                               .arg(runtime_status_.detail),
                           0);
}

void MainWindow::setStatusText(QLabel* label, QString text, bool ready, bool bypassed) {
  label->setText(std::move(text));
  setStyledProperty(label, "statusKind", bypassed ? "neutral" : ready ? "ready" : "warn");
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
  if (!config_dirty_ && validateControls()) {
    navigation_->setCurrentRow(kLivePageIndex);
    controller_->startSystem();
  }
}

void MainWindow::captureSegmentation() {
  controller_->captureSegmentationSnapshot();
}

void MainWindow::showPage(int index) {
  navigation_->setCurrentRow(std::clamp(index, 0, navigation_->count() - 1));
}

void MainWindow::showLiveTab(int index) {
  navigation_->setCurrentRow(kLivePageIndex);
  live_tabs_->setCurrentIndex(std::clamp(index, 0, live_tabs_->count() - 1));
}

}  // namespace fmcw
