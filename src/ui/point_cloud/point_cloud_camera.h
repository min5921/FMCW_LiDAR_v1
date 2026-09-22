#pragma once

#include <QLineF>
#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cmath>

namespace fmcw {

struct PointCloudCamera {
  float yaw_degrees = -35.0F;
  float pitch_degrees = 20.0F;
  float zoom = 1.0F;
  float pan_x = 0.0F;
  float pan_y = 0.0F;

  void zoomBy(float wheel_delta) {
    zoom = std::exp(std::clamp(std::log(zoom) + wheel_delta * std::log(1.0015F),
                               std::log(0.01F), std::log(200.0F)));
  }
};

// Coordinates are normalized by the initial fit. Updating clipping bounds never
// changes the camera pose or scale, even when a later frame is much farther away.
class PointCloudProjection {
 public:
  PointCloudProjection(const PointCloudCamera& camera, QSize viewport, float radius)
      : viewport_(viewport.expandedTo(QSize(1, 1))) {
    constexpr float radians = 0.017453292519943295F;
    const auto yaw = camera.yaw_degrees * radians;
    const auto pitch = camera.pitch_degrees * radians;
    const QVector3D toward(-std::cos(pitch) * std::sin(yaw),
                            std::cos(pitch) * std::cos(yaw), std::sin(pitch));
    const QVector3D up(std::sin(pitch) * std::sin(yaw),
                      -std::sin(pitch) * std::cos(yaw), std::cos(pitch));
    const auto distance = 3.4F / camera.zoom;
    const auto near_plane = std::min(0.01F, distance * 0.01F);
    const auto far_plane = std::max(10.0F, distance + std::max(1.0F, radius) * 1.2F + 1.0F);
    QMatrix4x4 view;
    view.lookAt(toward * distance, QVector3D(), up);
    const auto scale = std::min(viewport_.width(), viewport_.height()) * 0.72F;
    const auto half_width = near_plane * viewport_.width() / (2.0F * scale);
    const auto half_height = near_plane * viewport_.height() / (2.0F * scale);
    QMatrix4x4 projection;
    projection.frustum(-half_width, half_width, -half_height, half_height,
                       near_plane, far_plane);
    QMatrix4x4 pan;
    pan.translate(camera.pan_x, camera.pan_y, 0.0F);
    matrix_ = pan * projection * view;
  }

  const QMatrix4x4& matrix() const { return matrix_; }

  bool projectPoint(const QVector3D& point, QPointF& screen, float& depth) const {
    const auto clip = matrix_ * QVector4D(point, 1.0F);
    if (!finite(clip) || clip.w() <= 0.0F) return false;
    for (const auto distance : planeDistances(clip)) {
      if (distance < 0.0F) return false;
    }
    screen = toScreen(clip);
    depth = clip.z() / clip.w();
    return true;
  }

  bool projectSegment(const QVector3D& from, const QVector3D& to, QLineF& line) const {
    const auto a = matrix_ * QVector4D(from, 1.0F);
    const auto b = matrix_ * QVector4D(to, 1.0F);
    if (!finite(a) || !finite(b)) return false;
    const auto da = planeDistances(a);
    const auto db = planeDistances(b);
    float first = 0.0F;
    float last = 1.0F;
    // Clip before the perspective divide so grid/axis lines crossing the eye
    // cannot turn into unbounded screen-space lines.
    for (std::size_t i = 0; i < da.size(); ++i) {
      if (da[i] < 0.0F && db[i] < 0.0F) return false;
      if (da[i] < 0.0F) first = std::max(first, da[i] / (da[i] - db[i]));
      if (db[i] < 0.0F) last = std::min(last, da[i] / (da[i] - db[i]));
    }
    if (first > last) return false;
    const auto clipped_a = a + (b - a) * first;
    const auto clipped_b = a + (b - a) * last;
    if (clipped_a.w() <= 0.0F || clipped_b.w() <= 0.0F) return false;
    line = QLineF(toScreen(clipped_a), toScreen(clipped_b));
    return true;
  }

 private:
  static bool finite(const QVector4D& p) {
    return std::isfinite(p.x()) && std::isfinite(p.y()) &&
           std::isfinite(p.z()) && std::isfinite(p.w());
  }
  static std::array<float, 6> planeDistances(const QVector4D& p) {
    return {p.w() + p.x(), p.w() - p.x(), p.w() + p.y(),
            p.w() - p.y(), p.w() + p.z(), p.w() - p.z()};
  }
  QPointF toScreen(const QVector4D& p) const {
    return {(p.x() / p.w() + 1.0) * viewport_.width() * 0.5,
            (1.0 - p.y() / p.w()) * viewport_.height() * 0.5};
  }

  QSize viewport_;
  QMatrix4x4 matrix_;
};

}  // namespace fmcw
