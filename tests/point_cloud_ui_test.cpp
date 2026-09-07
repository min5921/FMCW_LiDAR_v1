#include "apps/common/main_window.h"
#include "storage/binary_storage.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

namespace {
bool waitFor(const std::function<bool()>& predicate) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 5000) {
    QApplication::processEvents();
    if (predicate()) return true;
    QThread::msleep(5);
  }
  return false;
}

fmcw::PointCloudSnapshot frame(unsigned index, unsigned count) {
  fmcw::PointCloudSnapshot result;
  result.scan_frame_index = index;
  result.last_frame_id = index;
  result.width = count;
  result.height = result.completed_lines = 1;
  result.complete = true;
  for (unsigned i = 0; i < count; ++i) {
    fmcw::PointXYZI point;
    point.x = 4.0F + index;
    point.y = static_cast<float>(i);
    point.z = 1.0F;
    point.intensity = -20.0F;
    point.velocity = 0.0F;
    point.valid = true;
    result.points.push_back(point);
  }
  return result;
}
}

int main(int argc, char** argv) {
#ifndef _WIN32
  if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
    std::cout << "UI test requires a display; reader tests remain available headlessly\n";
    return 77;
  }
#endif
  // An inherited model path must not add inference controls or load weights.
  qputenv("FMCW_CENTERPOINT_WEIGHTS_ROOT", "missing-model-must-not-be-used");
  QApplication app(argc, argv);
  fmcw::applyDarkApplicationTheme(app);
  QTemporaryDir directory;
  if (!directory.isValid()) return 1;
  const auto path = std::filesystem::u8path(directory.path().toUtf8().toStdString());
  {
    std::ofstream pcd(path / "test.pcd");
    pcd << "VERSION .7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n"
           "WIDTH 4\nHEIGHT 1\nPOINTS 4\nDATA ascii\n"
           "7 -1 0\n7 1 0\n7 -1 2\n7 1 2\n";
  }
  fmcw::WriterOpenOptions options;
  options.session_directory = path;
  options.file_stem = "sequence";
  options.session.session_id = "replay-ui-test";
  options.session.config_snapshot_yaml = "schema_version: 1\n";
  options.session.config_snapshot_json = "{}";
  fmcw::BinaryPointCloudFrameWriter writer;
  std::string error;
  if (!writer.open(options, error) || !writer.write(frame(0, 2), error) ||
      !writer.write(frame(1, 3), error) ||
      !writer.finalize({0, "test", true}, error)) {
    std::cerr << error << '\n';
    return 1;
  }

  fmcw::MainWindow window("Replay test");
  window.show();
  fmcw::PointCloudWidget* cloud = nullptr;
  for (auto* widget : window.findChildren<QWidget*>()) {
    if (auto* candidate = dynamic_cast<fmcw::PointCloudWidget*>(widget)) cloud = candidate;
  }
  auto* step = window.findChild<QToolButton*>("pointCloudReplayStep");
  auto* play = window.findChild<QToolButton*>("pointCloudReplayPlay");
  auto* stop = window.findChild<QToolButton*>("pointCloudReplayStop");
  if (!cloud || !step || !play || !stop ||
      window.findChild<QObject*>("objectDetectionWeightsButton") ||
      window.findChild<QObject*>("objectDetectionToggle")) return 1;
  cloud->setTemporalFusionFrames(1);
  cloud->setVerticalInterpolationFactor(1);

  if (!window.openPointCloudReplayFile(directory.filePath("test.pcd")) ||
      !waitFor([&] { return cloud->displayStats().source_valid_points == 4; }) ||
      play->isEnabled()) return 1;
  if (!window.openPointCloudReplayFile(directory.filePath("sequence.pointcloud.bin")) ||
      !waitFor([&] { return cloud->displayStats().source_valid_points == 2; }) ||
      !play->isEnabled()) return 1;
  step->click();
  if (!waitFor([&] { return cloud->displayStats().source_valid_points == 3; })) return 1;
  stop->click();
  if (!waitFor([&] { return cloud->displayStats().source_valid_points == 2; })) return 1;
  play->click();
  if (!waitFor([&] { return cloud->displayStats().source_valid_points == 3; })) return 1;
  play->click();
  QElapsedTimer paused;
  paused.start();
  while (paused.elapsed() < 250) {
    QApplication::processEvents();
    QThread::msleep(5);
  }
  if (cloud->displayStats().source_valid_points != 3 || play->isChecked()) return 1;
  std::cout << "PCD + multi-frame replay, step/rewind/play/pause work without device connection or weights\n";
  return 0;
}
