#pragma once

#include <QLabel>
#include <QMainWindow>

namespace fmcw {

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(QString platform_name, QWidget* parent = nullptr);

 private:
  QLabel* state_label_ = nullptr;
};

}  // namespace fmcw
