#include "apps/common/main_window.h"

#include "core/app_version.h"
#include "core/system_state.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace fmcw {

MainWindow::MainWindow(QString platform_name, QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QString("FMCW LiDAR v%1 - %2").arg(QString::fromStdString(versionString()), platform_name));
  resize(1280, 800);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(12);

  auto* title = new QLabel("FMCW LiDAR Control Console", central);
  QFont title_font = title->font();
  title_font.setPointSize(18);
  title_font.setBold(true);
  title->setFont(title_font);

  auto* subtitle = new QLabel("Phase 1 skeleton: core, platform entry points, and Qt shell are ready.", central);
  subtitle->setWordWrap(true);

  layout->addWidget(title);
  layout->addWidget(subtitle);
  layout->addStretch(1);

  setCentralWidget(central);

  state_label_ = new QLabel(QString("State: %1").arg(QString::fromStdString(toString(OperationState::Disconnected))), this);
  statusBar()->addPermanentWidget(state_label_);
}

}  // namespace fmcw
