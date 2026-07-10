#include "apps/common/main_window.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  fmcw::MainWindow window(QStringLiteral(FMCW_PLATFORM_NAME));
  window.show();

  if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
    QTimer::singleShot(100, &app, &QApplication::quit);
  }
  return app.exec();
}
