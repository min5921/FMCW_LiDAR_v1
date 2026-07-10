#include "apps/common/main_window.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  fmcw::MainWindow window(QStringLiteral(FMCW_PLATFORM_NAME));
  window.show();

  const auto arguments = app.arguments();
  if (arguments.contains(QStringLiteral("--demo-run"))) {
    QTimer::singleShot(200, &window, &fmcw::MainWindow::startDemo);
  }
  if (arguments.contains(QStringLiteral("--capture-segmentation"))) {
    QTimer::singleShot(1600, &window, &fmcw::MainWindow::captureSegmentation);
  }
  QString screenshot_path;
  int page_index = -1;
  int live_tab_index = -1;
  for (const auto& argument : arguments) {
    if (argument.startsWith(QStringLiteral("--screenshot="))) {
      screenshot_path = argument.mid(QStringLiteral("--screenshot=").size());
    } else if (argument.startsWith(QStringLiteral("--page="))) {
      page_index = argument.mid(QStringLiteral("--page=").size()).toInt();
    } else if (argument.startsWith(QStringLiteral("--live-tab="))) {
      live_tab_index = argument.mid(QStringLiteral("--live-tab=").size()).toInt();
    }
  }
  if (page_index >= 0) {
    QTimer::singleShot(arguments.contains(QStringLiteral("--demo-run")) ? 400 : 0, &window,
                       [&window, page_index] { window.showPage(page_index); });
  }
  if (live_tab_index >= 0) {
    QTimer::singleShot(arguments.contains(QStringLiteral("--demo-run")) ? 400 : 0, &window,
                       [&window, live_tab_index] { window.showLiveTab(live_tab_index); });
  }
  if (!screenshot_path.isEmpty()) {
    QTimer::singleShot(arguments.contains(QStringLiteral("--demo-run")) ? 5200 : 1000, &window,
                       [&app, &window, screenshot_path] {
                         QDir().mkpath(QFileInfo(screenshot_path).absolutePath());
                         window.repaint();
                         QApplication::processEvents(QEventLoop::AllEvents, 100);
                         app.exit(window.grab().save(screenshot_path, "PNG") ? 0 : 2);
                       });
  } else if (arguments.contains(QStringLiteral("--smoke-test"))) {
    QTimer::singleShot(100, &app, &QApplication::quit);
  }
  return app.exec();
}
