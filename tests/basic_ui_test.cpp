#include "ui/main_window.h"

#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QTabWidget>
#include <QToolButton>

#include <iostream>

int main(int argc, char** argv) {
  qputenv("FMCW_CENTERPOINT_WEIGHTS_ROOT", "model-path-must-not-enable-detection");
  QApplication app(argc, argv);
  fmcw::MainWindow window("Windows");

  const auto* navigation = window.findChild<QListWidget*>("navigation");
  if (!navigation || navigation->count() != 8 || navigation->currentRow() != 0 ||
      navigation->item(0)->text() != "Overview") {
    std::cerr << "Basic application must retain the acquisition pages and open Overview\n";
    return 1;
  }
  bool acquisition_source = false;
  for (const auto* combo : window.findChildren<QComboBox*>()) {
    if (combo->findText("Simulator") >= 0 && combo->findText("AlazarTech ATS") >= 0 &&
        combo->findText("Raw Replay") >= 0) acquisition_source = true;
  }
  bool raw_file_control = false;
  for (const auto* button : window.findChildren<QToolButton*>()) {
    if (button->toolTip() == "Select raw replay file") raw_file_control = true;
    if (button->toolTip().contains("point-cloud replay", Qt::CaseInsensitive)) return 1;
  }
  bool cloud_tab = false;
  for (const auto* tabs : window.findChildren<QTabWidget*>()) {
    for (int index = 0; index < tabs->count(); ++index) {
      if (tabs->tabText(index) == "3D Point Cloud") cloud_tab = true;
    }
  }
  for (const auto* name : {"pointCloudReplayPlay", "pointCloudReplayStep",
                          "pointCloudReplayStop", "objectDetectionWeightsButton",
                          "objectDetectionToggle"}) {
    if (window.findChild<QObject*>(name)) return 1;
  }
  if (!acquisition_source || !raw_file_control || !cloud_tab) {
    std::cerr << "Basic acquisition, RAW replay or live point-cloud display was removed\n";
    return 1;
  }
  std::cout << "Basic workspace retains acquisition/RAW/live 3D and excludes PCD/model UI\n";
  return 0;
}
