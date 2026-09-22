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
    if (!std::isfinite(wheel_delta)) return;
    // Numerical guards only: six orders of magnitude on either side of Fit View.
    // Logarithmic updates also handle large wheel/trackpad deltas without overflow.
    zoom = static_cast<float>(std::exp(std::clamp(
        std::log(static_cast<double>(zoom)) + wheel_delta * std::log(1.0015),
        std::log(1.0e-6), std::log(1.0e6))));
  }

  void zoomAt(float wheel_delta, const QPointF& cursor, QSize viewport) {
    if (viewport.width() <= 0 || viewport.height() <= 0 ||
        !std::isfinite(cursor.x()) || !std::isfinite(cursor.y())) return;
    const double previous_zoom = zoom;
    zoomBy(wheel_delta);
    const double ratio = zoom / previous_zoom;
    // Projection magnifies every depth by the same ratio. Compensate the pan in
    // NDC so the ray under the pointer stays at that pixel, even after panning.
    // Use the actual ratio so hitting a numerical zoom guard cannot move the view.
    const double anchor_x = 2.0 * cursor.x() / viewport.width() - 1.0;
    const double anchor_y = 1.0 - 2.0 * cursor.y() / viewport.height();
    pan_x = static_cast<float>(anchor_x + ratio * (pan_x - anchor_x));
    pan_y = static_cast<float>(anchor_y + ratio * (pan_y - anchor_y));
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
    // Magnify the view instead of driving the eye through the measured points.
    // This also keeps depth precision independent of the zoom factor.
    constexpr float distance = 3.4F;
    constexpr float near_plane = 0.01F;
    const auto far_plane = std::max(10.0F, distance + std::max(1.0F, radius) * 1.2F + 1.0F);
    QMatrix4x4 view;
    view.lookAt(toward * distance, QVector3D(), up);
    const auto scale = std::min(viewport_.width(), viewport_.height()) * 0.72F * camera.zoom;
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
    double first = 0.0;
    double last = 1.0;
    // Clip before the perspective divide so grid/axis lines crossing the eye
    // cannot turn into unbounded screen-space lines.
    for (std::size_t i = 0; i < da.size(); ++i) {
      if (da[i] < 0.0F && db[i] < 0.0F) return false;
      if (da[i] < 0.0F) first = std::max(first, da[i] / (da[i] - db[i]));
      if (db[i] < 0.0F) last = std::min(last, da[i] / (da[i] - db[i]));
    }
    if (first > last) return false;
    const auto clipped_a = interpolateClip(a, b, first);
    const auto clipped_b = interpolateClip(a, b, last);
    if (clipped_a.w() <= 0.0F || clipped_b.w() <= 0.0F) return false;
    line = QLineF(toScreen(clipped_a), toScreen(clipped_b));
    return true;
  }

 private:
  static bool finite(const QVector4D& p) {
    return std::isfinite(p.x()) && std::isfinite(p.y()) &&
           std::isfinite(p.z()) && std::isfinite(p.w());
  }
  static std::array<double, 6> planeDistances(const QVector4D& p) {
    const double w = p.w();
    return {w + p.x(), w - p.x(), w + p.y(), w - p.y(), w + p.z(), w - p.z()};
  }
  static QVector4D interpolateClip(const QVector4D& a, const QVector4D& b, double t) {
    // At high magnification a line can extend millions of pixels offscreen.
    // Keep the intersection calculation in double precision until after clipping.
    QVector4D result;
    for (int i = 0; i < 4; ++i) {
      result[i] = static_cast<float>(static_cast<double>(a[i]) +
          (static_cast<double>(b[i]) - a[i]) * t);
    }
    return result;
  }
  QPointF toScreen(const QVector4D& p) const {
    return {(p.x() / p.w() + 1.0) * viewport_.width() * 0.5,
            (1.0 - p.y() / p.w()) * viewport_.height() * 0.5};
  }

  QSize viewport_;
  QMatrix4x4 matrix_;
};

}  // namespace fmcw
