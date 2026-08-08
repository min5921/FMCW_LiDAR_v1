#include "apps/common/point_cloud_widget.h"

#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QTextStream>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace fmcw {
namespace {

void colorMap(float value, float& red, float& green, float& blue) {
  const auto t = std::clamp(value, 0.0F, 1.0F);
  if (t < 0.25F) {
    const auto u = t * 4.0F;
    red = 0.08F;
    green = 0.25F + 0.55F * u;
    blue = 0.75F + 0.15F * u;
  } else if (t < 0.5F) {
    const auto u = (t - 0.25F) * 4.0F;
    red = 0.08F + 0.12F * u;
    green = 0.80F + 0.12F * u;
    blue = 0.90F - 0.55F * u;
  } else if (t < 0.75F) {
    const auto u = (t - 0.5F) * 4.0F;
    red = 0.20F + 0.75F * u;
    green = 0.92F - 0.12F * u;
    blue = 0.35F - 0.25F * u;
  } else {
    const auto u = (t - 0.75F) * 4.0F;
    red = 0.95F;
    green = 0.80F - 0.65F * u;
    blue = 0.10F + 0.05F * u;
  }
}

}  // namespace

PointCloudWidget::PointCloudWidget(QWidget* parent) : QOpenGLWidget(parent) {
  setMinimumSize(480, 320);
  setFocusPolicy(Qt::StrongFocus);
  setToolTip("Drag to rotate, right-drag to pan, and use the wheel to zoom");
}

PointCloudWidget::~PointCloudWidget() = default;

void PointCloudWidget::setSnapshot(std::shared_ptr<const PointCloudSnapshot> snapshot) {
  if (!snapshot || !snapshot->complete || snapshot == snapshot_) {
    return;
  }
  const bool new_session = snapshot_ != nullptr &&
      snapshot->scan_frame_index <= snapshot_->scan_frame_index;
  if (new_session) {
    spatial_bounds_valid_ = false;
  }
  snapshot_ = std::move(snapshot);
  current_points_.clear();
  std::copy_if(snapshot_->points.begin(), snapshot_->points.end(), std::back_inserter(current_points_),
               [](const PointXYZI& point) { return point.valid; });
  rebuildVertices();
  if (!spatial_bounds_valid_) {
    fitSpatialBounds();
  }
  update();
}

void PointCloudWidget::setColorMode(PointCloudColorMode mode) {
  color_mode_ = mode;
  rebuildVertices();
  update();
}

void PointCloudWidget::setPointSize(float pixels) {
  point_size_ = std::clamp(pixels, 1.0F, 12.0F);
  update();
}

void PointCloudWidget::setAxesVisible(bool visible) {
  axes_visible_ = visible;
  update();
}

void PointCloudWidget::resetCamera() {
  yaw_degrees_ = -35.0F;
  pitch_degrees_ = -20.0F;
  zoom_ = 1.0F;
  pan_x_ = 0.0F;
  pan_y_ = 0.0F;
  fitSpatialBounds();
  update();
}

bool PointCloudWidget::saveCurrentCloud(const QString& path) const {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return false;
  }
  QTextStream stream(&file);
  stream << "x_forward_m,y_left_m,z_up_m,intensity_db,velocity_mps,"
            "scan_x_command,scan_y_command\n";
  for (const auto& point : current_points_) {
    stream << point.x << ',' << point.y << ',' << point.z << ',' << point.intensity << ','
           << point.velocity << ',' << point.scan_x_command << ',' << point.scan_y_command << '\n';
  }
  return stream.status() == QTextStream::Ok;
}

void PointCloudWidget::initializeGL() {
  initializeOpenGLFunctions();
  glClearColor(0.035F, 0.055F, 0.063F, 1.0F);
}

void PointCloudWidget::resizeGL(int, int) {}

void PointCloudWidget::paintGL() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#091013"));
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);

  const auto yaw = static_cast<double>(yaw_degrees_) * 3.14159265358979323846 / 180.0;
  const auto pitch = static_cast<double>(pitch_degrees_) * 3.14159265358979323846 / 180.0;
  const auto cosine_yaw = std::cos(yaw);
  const auto sine_yaw = std::sin(yaw);
  const auto cosine_pitch = std::cos(pitch);
  const auto sine_pitch = std::sin(pitch);
  const auto canvas_scale = static_cast<double>(std::min(width(), height())) * 0.72;
  const auto project = [&](double x, double y, double z) {
    const auto rotated_x = cosine_yaw * x + sine_yaw * y;
    const auto yaw_depth = -sine_yaw * x + cosine_yaw * y;
    const auto rotated_vertical = cosine_pitch * z - sine_pitch * yaw_depth;
    const auto rotated_depth = sine_pitch * z + cosine_pitch * yaw_depth;
    const auto depth = std::max(0.2, 3.4 / static_cast<double>(zoom_) - rotated_depth);
    return QPointF(width() * (0.5 + 0.5 * pan_x_) + rotated_x * canvas_scale / depth,
                   height() * (0.5 - 0.5 * pan_y_) - rotated_vertical * canvas_scale / depth);
  };
  const auto projectWorld = [&](double x, double y, double z) {
    return project((x - static_cast<double>(center_x_)) * 2.0 / extent_,
                   (y - static_cast<double>(center_y_)) * 2.0 / extent_,
                   (z - static_cast<double>(center_z_)) * 2.0 / extent_);
  };

  painter.setPen(QPen(QColor(52, 72, 78, 120), 1.0));
  for (int index = -4; index <= 4; ++index) {
    const auto coordinate = static_cast<double>(index) / 4.0;
    const auto x = static_cast<double>(center_x_) + coordinate * extent_ * 0.5;
    const auto y = static_cast<double>(center_y_) + coordinate * extent_ * 0.5;
    painter.drawLine(projectWorld(x, center_y_ - extent_ * 0.5, 0.0),
                     projectWorld(x, center_y_ + extent_ * 0.5, 0.0));
    painter.drawLine(projectWorld(center_x_ - extent_ * 0.5, y, 0.0),
                     projectWorld(center_x_ + extent_ * 0.5, y, 0.0));
  }

  if (axes_visible_) {
    const auto axis_length = static_cast<double>(extent_) * 0.325;
    const auto origin = projectWorld(0.0, 0.0, 0.0);
    const auto x_axis = projectWorld(axis_length, 0.0, 0.0);
    const auto y_axis = projectWorld(0.0, axis_length, 0.0);
    const auto z_axis = projectWorld(0.0, 0.0, axis_length);
    painter.setPen(QPen(QColor("#e05a67"), 2.0));
    painter.drawLine(origin, x_axis);
    painter.drawText(x_axis + QPointF(4.0, 0.0), "X");
    painter.setPen(QPen(QColor("#67c98c"), 2.0));
    painter.drawLine(origin, y_axis);
    painter.drawText(y_axis + QPointF(4.0, 0.0), "Y");
    painter.setPen(QPen(QColor("#55aee6"), 2.0));
    painter.drawLine(origin, z_axis);
    painter.drawText(z_axis + QPointF(4.0, 0.0), "Z");
  }

  painter.setPen(Qt::NoPen);
  const auto point_radius = static_cast<double>(point_size_) * 0.5;
  for (const auto& vertex : vertices_) {
    const auto normalized_x = static_cast<double>(vertex.x - center_x_) * 2.0 / extent_;
    const auto normalized_y = static_cast<double>(vertex.y - center_y_) * 2.0 / extent_;
    const auto normalized_z = static_cast<double>(vertex.z - center_z_) * 2.0 / extent_;
    const auto screen = project(normalized_x, normalized_y, normalized_z);
    if (screen.x() < -point_size_ || screen.x() > width() + point_size_ ||
        screen.y() < -point_size_ || screen.y() > height() + point_size_) {
      continue;
    }
    painter.setBrush(QColor::fromRgbF(vertex.r, vertex.g, vertex.b));
    painter.drawEllipse(screen, point_radius, point_radius);
  }

  painter.setPen(QColor("#aebdc1"));
  const auto frame_text = snapshot_
      ? QString("Frame %1 complete | %2 points")
            .arg(snapshot_->scan_frame_index + 1U)
            .arg(current_points_.size())
      : QString("Waiting for complete raster frame");
  painter.drawText(QRect(14, 12, width() - 28, 24), Qt::AlignLeft | Qt::AlignVCenter, frame_text);
  if (axes_visible_) {
    painter.setPen(QColor("#71858b"));
    painter.drawText(QRect(14, height() - 34, width() - 28, 22), Qt::AlignLeft | Qt::AlignVCenter,
                     "X forward | Y left | Z up | meters");
  }
}

void PointCloudWidget::mousePressEvent(QMouseEvent* event) {
  last_mouse_position_ = event->position().toPoint();
}

void PointCloudWidget::mouseMoveEvent(QMouseEvent* event) {
  const auto position = event->position().toPoint();
  const auto delta = position - last_mouse_position_;
  last_mouse_position_ = position;
  if ((event->buttons() & Qt::LeftButton) != 0) {
    yaw_degrees_ += static_cast<float>(delta.x()) * 0.45F;
    pitch_degrees_ = std::clamp(pitch_degrees_ + static_cast<float>(delta.y()) * 0.45F, -89.0F, 89.0F);
  } else if ((event->buttons() & (Qt::RightButton | Qt::MiddleButton)) != 0) {
    pan_x_ += static_cast<float>(delta.x()) / std::max(1, width()) * 2.0F;
    pan_y_ -= static_cast<float>(delta.y()) / std::max(1, height()) * 2.0F;
  }
  update();
}

void PointCloudWidget::wheelEvent(QWheelEvent* event) {
  zoom_ = std::clamp(zoom_ * std::pow(1.0015F, static_cast<float>(event->angleDelta().y())), 0.2F, 8.0F);
  update();
}

void PointCloudWidget::rebuildVertices() {
  vertices_.clear();
  if (current_points_.empty()) {
    return;
  }
  float minimum_value = std::numeric_limits<float>::max();
  float maximum_value = std::numeric_limits<float>::lowest();
  const auto valueFor = [this](const PointXYZI& point) {
    if (color_mode_ == PointCloudColorMode::Intensity) return point.intensity;
    if (color_mode_ == PointCloudColorMode::Velocity) return point.velocity;
    return std::sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
  };
  for (const auto& point : current_points_) {
    const auto value = valueFor(point);
    minimum_value = std::min(minimum_value, value);
    maximum_value = std::max(maximum_value, value);
  }
  const auto value_span = std::max(maximum_value - minimum_value, 1.0e-6F);
  vertices_.reserve(current_points_.size());
  for (const auto& point : current_points_) {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    colorMap((valueFor(point) - minimum_value) / value_span, red, green, blue);
    vertices_.push_back({point.x, point.y, point.z, red, green, blue});
  }
}

void PointCloudWidget::fitSpatialBounds() {
  if (current_points_.empty()) {
    center_x_ = center_y_ = center_z_ = 0.0F;
    extent_ = 1.0F;
    spatial_bounds_valid_ = false;
    return;
  }
  float minimum_x = std::numeric_limits<float>::max();
  float minimum_y = std::numeric_limits<float>::max();
  float minimum_z = std::numeric_limits<float>::max();
  float maximum_x = std::numeric_limits<float>::lowest();
  float maximum_y = std::numeric_limits<float>::lowest();
  float maximum_z = std::numeric_limits<float>::lowest();
  for (const auto& point : current_points_) {
    minimum_x = std::min(minimum_x, point.x);
    minimum_y = std::min(minimum_y, point.y);
    minimum_z = std::min(minimum_z, point.z);
    maximum_x = std::max(maximum_x, point.x);
    maximum_y = std::max(maximum_y, point.y);
    maximum_z = std::max(maximum_z, point.z);
  }
  center_x_ = 0.5F * (minimum_x + maximum_x);
  center_y_ = 0.5F * (minimum_y + maximum_y);
  center_z_ = 0.5F * (minimum_z + maximum_z);
  extent_ = std::max({maximum_x - minimum_x, maximum_y - minimum_y,
                      maximum_z - minimum_z, 1.0e-4F});
  spatial_bounds_valid_ = true;
}

}  // namespace fmcw
