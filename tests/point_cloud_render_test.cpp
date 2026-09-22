#include "ui/point_cloud/point_cloud_widget.h"
#include "support/point_cloud_replay/point_cloud_replay.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
unsigned sequence = 0;
constexpr float radians = 0.017453292519943295F;
const fmcw::PointCloudCamera initial;
const float yaw = initial.yaw_degrees * radians;
const float pitch = initial.pitch_degrees * radians;
const QVector3D toward(-std::cos(pitch) * std::sin(yaw),
                        std::cos(pitch) * std::cos(yaw), std::sin(pitch));
const QVector3D up(std::sin(pitch) * std::sin(yaw),
                  -std::sin(pitch) * std::cos(yaw), std::cos(pitch));
const QVector3D right = QVector3D::crossProduct(up, toward);

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

fmcw::PointXYZI point(float r, float u, float d, float intensity = 0.0F) {
  const auto v = right * r + up * u + toward * d;
  fmcw::PointXYZI p;
  p.x = v.x(); p.y = v.y(); p.z = v.z();
  p.intensity = intensity; p.valid = true;
  return p;
}

std::shared_ptr<fmcw::PointCloudSnapshot> snapshot(std::vector<fmcw::PointXYZI> points) {
  auto result = std::make_shared<fmcw::PointCloudSnapshot>();
  result->complete = true;
  result->height = result->completed_lines = 1;
  result->scan_frame_index = result->last_frame_id = ++sequence;
  result->width = static_cast<unsigned>(points.size());
  result->points = std::move(points);
  return result;
}

std::shared_ptr<fmcw::PointCloudSnapshot> plane(float depth, bool anchor = false) {
  std::vector<fmcw::PointXYZI> points;
  for (int i = -2; i <= 2; ++i) {
    for (int j = -2; j <= 2; ++j) points.push_back(point(i * 0.04F, j * 0.04F, depth));
  }
  if (anchor) points.push_back(point(0, 0, 1));
  return snapshot(std::move(points));
}

void zoom(fmcw::PointCloudWidget& widget, int delta) {
  QWheelEvent event({320, 240}, {320, 240}, {}, {0, delta}, Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&widget, &event);
}

void drag(fmcw::PointCloudWidget& widget, Qt::MouseButton button, QPointF delta) {
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 200), QPointF(200, 200),
                    button, button, Qt::NoModifier);
  QMouseEvent move(QEvent::MouseMove, QPointF(200, 200) + delta, QPointF(200, 200) + delta,
                   Qt::NoButton, button, Qt::NoModifier);
  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(200, 200) + delta,
                      QPointF(200, 200) + delta, button, Qt::NoButton, Qt::NoModifier);
  const std::array<QEvent*, 3> events{&press, &move, &release};
  for (auto* event : events) {
    QApplication::sendEvent(&widget, event);
  }
}

bool blue(const QColor& c) {
  return c.blue() > c.green() + 30 && c.blue() > c.red() + 50;
}

struct Capture {
  QImage image;
  int pixels = 0;
  QRect bounds;
};

Capture capture(fmcw::PointCloudWidget& widget, const QString& directory, const char* name) {
  QApplication::processEvents();
  Capture result;
  result.image = widget.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
  check(!result.image.isNull(), "No OpenGL framebuffer available");
  int min_x = result.image.width(), min_y = result.image.height(), max_x = -1, max_y = -1;
  for (int y = 0; y < result.image.height(); ++y) {
    for (int x = 0; x < result.image.width(); ++x) {
      if (blue(result.image.pixelColor(x, y))) {
        ++result.pixels;
        min_x = std::min(min_x, x); max_x = std::max(max_x, x);
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
      }
    }
  }
  if (result.pixels) result.bounds = QRect(QPoint(min_x, min_y), QPoint(max_x, max_y));
  check(result.image.save(directory + '/' + name + ".png"), "Cannot save framebuffer evidence");
  std::cout << name << " points=" << widget.displayStats().displayed_points
            << " pixels=" << result.pixels << " bounds=" << result.bounds.width()
            << 'x' << result.bounds.height() << " dpr=" << widget.devicePixelRatioF() << '\n';
  return result;
}
}

int main(int argc, char** argv) {
#ifndef _WIN32
  if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
    std::cout << "Rendering tests require a display; camera math tests run headlessly\n";
    return 77;
  }
#endif
  QApplication app(argc, argv);
  try {
    const bool painter = app.arguments().contains("--painter");
    fmcw::PointCloudWidget widget(nullptr, painter ? fmcw::PointCloudRenderer::Painter
                                                 : fmcw::PointCloudRenderer::Automatic);
    widget.resize(640, 480);
    widget.setAxesVisible(false);
    widget.setPointSize(8);
    widget.setTemporalFusionFrames(1);
    widget.setVerticalInterpolationFactor(1);
    widget.show();
    const auto directory = QCoreApplication::applicationDirPath() + "/projection-" +
        (painter ? "painter-" : "gpu-") + QString::number(widget.devicePixelRatioF());
    check(QDir().mkpath(directory), "Cannot create screenshot directory");
    widget.setSnapshot(plane(0, true));
    const auto baseline = capture(widget, directory, "01_default");
    check(widget.gpuRendererReady() != painter, "Requested renderer was not initialized");
    check(baseline.pixels > 0, "Default view is blank");
    zoom(widget, -12000);
    check(capture(widget, directory, "02_zoom_out").pixels > 0, "Zoom-out lost all points");
    widget.resetCamera();
    check(capture(widget, directory, "03_reset").bounds == baseline.bounds, "Reset must restore framing");
    widget.clearSnapshot();
    widget.setSnapshot(plane(1));
    zoom(widget, 1440);
    check(capture(widget, directory, "04_behind_camera").pixels == 0, "Behind-eye points were painted");
    widget.clearSnapshot();
    widget.setSnapshot(plane(0, true));
    widget.setSnapshot(plane(-10));
    check(capture(widget, directory, "05_later_far_frame").pixels > 0, "Farther frame disappeared");
    widget.clearSnapshot();
    widget.setSnapshot(plane(0, true));
    drag(widget, Qt::RightButton, {900, 0});
    check(capture(widget, directory, "06_pan_offscreen").pixels == 0, "Pan reproduction failed");
    widget.clearSnapshot(false);
    widget.setSnapshot(plane(0, true));
    check(capture(widget, directory, "07_rewind_preserves_camera").pixels == 0,
          "Rewind unexpectedly reset the camera");
    widget.clearSnapshot();
    widget.setSnapshot(plane(0, true));
    check(capture(widget, directory, "08_new_file_resets_camera").bounds == baseline.bounds,
          "A new file retained the previous offscreen pan");
    widget.clearSnapshot();
    widget.setSnapshot(snapshot({point(0, 0, 0)}));
    const auto dot = capture(widget, directory, "09_point_size");
    const auto expected_size = 8.0 * widget.devicePixelRatioF();
    check(std::abs(dot.bounds.width() - expected_size) <= 2 &&
              std::abs(dot.bounds.height() - expected_size) <= 2, "Point size does not scale with DPI");
    widget.clearSnapshot();
    widget.setSnapshot(snapshot({point(0, 0, 1, 0), point(0, 0, -1, 1)}));
    const auto overlap = capture(widget, directory, "10_near_occludes_far");
    check(blue(overlap.image.pixelColor(overlap.image.width() / 2, overlap.image.height() / 2)),
          "A farther, later point overwrote the nearer point");
    widget.clearSnapshot();
    widget.setSnapshot(snapshot({point(-0.2F, -0.2F, 0), point(0.2F, -0.2F, 0),
        point(-0.2F, 0.2F, 0), point(0.2F, 0.2F, 0), point(0, 0, 1)}));
    widget.resize(960, 320);
    const auto wide = capture(widget, directory, "11_wide_square");
    check(std::abs(wide.bounds.width() - wide.bounds.height()) <= 2, "Wide window distorted a square");
    widget.resize(480, 960);
    const auto tall = capture(widget, directory, "12_tall_square");
    check(std::abs(tall.bounds.width() - tall.bounds.height()) <= 2, "Tall window distorted a square");
    widget.resize(640, 480);
    widget.setAxesVisible(true);
    drag(widget, Qt::LeftButton, {180, 45});
    check(capture(widget, directory, "13_rotated_axes").pixels > 0, "Rotated cloud disappeared");
    widget.setAxesVisible(false);
    widget.clearSnapshot();
    widget.setSnapshot(snapshot({point(0, 0, 1)}));
    const auto front = capture(widget, directory, "14_front_point");
    check(front.pixels > 0, "Orbit direction marker is missing");
    drag(widget, Qt::LeftButton, {40, 0});
    const auto right_drag = capture(widget, directory, "15_drag_right");
    check(right_drag.pixels > 0 && right_drag.bounds.center().x() > front.bounds.center().x(),
          "Right drag must move the front of the cloud right, not left");
    widget.resetCamera();
    drag(widget, Qt::LeftButton, {-40, 0});
    const auto left_drag = capture(widget, directory, "16_drag_left");
    check(left_drag.pixels > 0 && left_drag.bounds.center().x() < front.bounds.center().x(),
          "Left drag must move the front of the cloud left, not right");
    widget.resetCamera();
    drag(widget, Qt::LeftButton, {0, 40});
    const auto down_drag = capture(widget, directory, "17_drag_down");
    check(down_drag.pixels > 0 && down_drag.bounds.center().y() > front.bounds.center().y(),
          "Vertical orbit direction must continue to follow the pointer");
    widget.resetCamera();
    drag(widget, Qt::RightButton, {40, 0});
    const auto pan_right = capture(widget, directory, "18_pan_right");
    check(pan_right.pixels > 0 &&
              std::abs(pan_right.bounds.center().x() - front.bounds.center().x() -
                       40.0 * widget.devicePixelRatioF()) <= 2,
          "Right-button pan must retain its direction and screen-space scale");
    for (const auto& argument : app.arguments()) {
      if (!argument.startsWith("--sample=")) continue;
      fmcw::PointCloudReplayReader reader;
      std::string error;
      const auto path = std::filesystem::u8path(argument.mid(9).toUtf8().toStdString());
      if (!reader.open(path, error)) throw std::runtime_error(error);
      widget.clearSnapshot();
      widget.resize(1280, 800);
      widget.setPointSize(3);
      for (int index = 0; index < 3; ++index) {
        auto frame = std::make_shared<fmcw::PointCloudSnapshot>();
        const auto status = reader.readNext(*frame, error);
        if (status == fmcw::PointCloudReadResult::EndOfStream && index > 0) break;
        if (status != fmcw::PointCloudReadResult::FrameReady) throw std::runtime_error(error);
        widget.setSnapshot(frame);
        capture(widget, directory, ("sample_frame_" + std::to_string(index)).c_str());
      }
      zoom(widget, 960);
      capture(widget, directory, "sample_zoom_in");
    }
    std::cout << "Projection framebuffer regressions passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
