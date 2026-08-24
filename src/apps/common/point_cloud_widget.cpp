#include "apps/common/point_cloud_widget.h"

#include <QFile>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QVector2D>
#include <QVector3D>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>

namespace fmcw {
namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr auto kPointVertexShaderBody = R"glsl(
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;

uniform vec3 cloud_center;
uniform float cloud_extent;
uniform float yaw_radians;
uniform float pitch_radians;
uniform float zoom_factor;
uniform vec2 pan_offset;
uniform vec2 viewport_size;
uniform float point_size;

out vec4 vertex_color;

void main() {
  vec3 point = (in_position - cloud_center) * (2.0 / max(cloud_extent, 1.0e-6));
  float cosine_yaw = cos(yaw_radians);
  float sine_yaw = sin(yaw_radians);
  float cosine_pitch = cos(pitch_radians);
  float sine_pitch = sin(pitch_radians);
  float rotated_x = cosine_yaw * point.x + sine_yaw * point.y;
  float yaw_depth = -sine_yaw * point.x + cosine_yaw * point.y;
  float rotated_vertical = cosine_pitch * point.z - sine_pitch * yaw_depth;
  float rotated_depth = sine_pitch * point.z + cosine_pitch * yaw_depth;
  float depth = max(0.2, 3.4 / max(zoom_factor, 0.05) - rotated_depth);
  float canvas_scale = min(viewport_size.x, viewport_size.y) * 0.72;
  float screen_x = viewport_size.x * (0.5 + 0.5 * pan_offset.x) +
      rotated_x * canvas_scale / depth;
  float screen_y = viewport_size.y * (0.5 - 0.5 * pan_offset.y) -
      rotated_vertical * canvas_scale / depth;
  vec2 ndc = vec2(2.0 * screen_x / max(viewport_size.x, 1.0) - 1.0,
                  1.0 - 2.0 * screen_y / max(viewport_size.y, 1.0));
  float normalized_depth = clamp((depth - 0.2) / 8.0, 0.0, 1.0);
  gl_Position = vec4(ndc, normalized_depth * 2.0 - 1.0, 1.0);
  gl_PointSize = point_size;
  vertex_color = in_color;
}
)glsl";

constexpr auto kPointFragmentShaderBody = R"glsl(
in vec4 vertex_color;
out vec4 fragment_color;

void main() {
  float radius = length(gl_PointCoord - vec2(0.5));
  if (radius > 0.5) {
    discard;
  }
  float coverage = 1.0 - smoothstep(0.38, 0.5, radius);
  fragment_color = vec4(vertex_color.rgb, vertex_color.a * coverage);
}
)glsl";

QString shaderSource(bool open_gl_es, const char* body) {
  const auto header = open_gl_es
      ? QStringLiteral("#version 300 es\nprecision highp float;\n")
      : QStringLiteral("#version 330 core\n");
  return header + QString::fromLatin1(body);
}

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

QColor detectionColor(ObjectClass object_class) {
  switch (object_class) {
    case ObjectClass::Vehicle:
      return QColor("#ffb347");
    case ObjectClass::Pedestrian:
      return QColor("#63d7ff");
    case ObjectClass::Cyclist:
      return QColor("#d77cff");
    case ObjectClass::Unknown:
      return QColor("#d7dde0");
  }
  return QColor("#d7dde0");
}

}  // namespace

PointCloudWidget::PointCloudWidget(QWidget* parent) : QOpenGLWidget(parent) {
  auto surface_format = format();
  const bool open_gl_es = QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES;
  surface_format.setRenderableType(open_gl_es ? QSurfaceFormat::OpenGLES
                                               : QSurfaceFormat::OpenGL);
  surface_format.setVersion(3, open_gl_es ? 0 : 3);
  surface_format.setProfile(open_gl_es ? QSurfaceFormat::NoProfile
                                       : QSurfaceFormat::CoreProfile);
  surface_format.setDepthBufferSize(24);
  setFormat(surface_format);
  setMinimumSize(480, 320);
  setFocusPolicy(Qt::StrongFocus);
  setToolTip("Drag to rotate, right-drag to pan, and use the wheel to zoom");
}

PointCloudWidget::~PointCloudWidget() {
  if (context() != nullptr) {
    makeCurrent();
    vertex_array_.destroy();
    vertex_buffer_.destroy();
    point_program_.reset();
    doneCurrent();
  }
}

void PointCloudWidget::setSnapshot(std::shared_ptr<const PointCloudSnapshot> snapshot) {
  if (!snapshot || !snapshot->complete || snapshot == snapshot_) {
    return;
  }
  if (!post_processor_.push(snapshot)) {
    return;
  }
  snapshot_ = std::move(snapshot);
  if (pending_detections_ != nullptr &&
      pending_detections_->last_frame_id <= snapshot_->last_frame_id) {
    if (detections_ == nullptr ||
        pending_detections_->last_frame_id >= detections_->last_frame_id) {
      detections_ = std::move(pending_detections_);
    } else {
      pending_detections_.reset();
    }
  }
  rebuildDisplayCloud();
  if (!spatial_bounds_valid_) {
    fitSpatialBounds();
  }
  update();
}

void PointCloudWidget::clearSnapshot() {
  snapshot_.reset();
  detections_.reset();
  pending_detections_.reset();
  post_processor_.reset();
  current_points_.clear();
  vertices_.clear();
  vertices_dirty_ = true;
  spatial_bounds_valid_ = false;
  center_x_ = center_y_ = center_z_ = 0.0F;
  extent_ = 1.0F;
  update();
}

void PointCloudWidget::setDetections(DetectionSnapshotPtr detections) {
  if (detections == nullptr) {
    if (detections_ == nullptr && pending_detections_ == nullptr) {
      return;
    }
    detections_.reset();
    pending_detections_.reset();
    update();
    return;
  }

  // Detection and point-cloud rendering run independently. Keep results for a
  // frame that the throttled UI has not displayed yet out of the active
  // overlay, otherwise the current box disappears until the display catches
  // up. The newest such result is promoted by setSnapshot().
  if (snapshot_ != nullptr && detections->last_frame_id > snapshot_->last_frame_id) {
    if (pending_detections_ == nullptr ||
        detections->last_frame_id >= pending_detections_->last_frame_id) {
      pending_detections_ = std::move(detections);
    }
    return;
  }
  if (detections == detections_ ||
      (detections_ != nullptr &&
       detections->last_frame_id < detections_->last_frame_id)) {
    return;
  }
  detections_ = std::move(detections);
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

void PointCloudWidget::setTemporalFusionFrames(std::uint32_t frame_count) {
  post_processor_.setHistoryFrameCount(frame_count);
  rebuildDisplayCloud();
  if (!spatial_bounds_valid_) {
    fitSpatialBounds();
  }
  update();
}

void PointCloudWidget::setVerticalInterpolationFactor(std::uint32_t factor) {
  post_processor_.setVerticalInterpolationFactor(factor);
  rebuildDisplayCloud();
  if (!spatial_bounds_valid_) {
    fitSpatialBounds();
  }
  update();
}

PointCloudDisplayStats PointCloudWidget::displayStats() const {
  const auto& display = post_processor_.displayFrame();
  PointCloudDisplayStats stats;
  stats.source_valid_points = display.source_valid_point_count;
  stats.fused_points = display.fused_point_count;
  stats.interpolated_points = display.interpolated_point_count;
  stats.displayed_points = display.displayedPointCount();
  stats.source_height = display.source_height;
  stats.display_height = display.display_height;
  return stats;
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
  stream << "x_forward_m,y_left_m,z_up_m,intensity,elongation,velocity_mps,"
            "scan_x_command,scan_y_command,display_interpolated,temporal_observations\n";
  for (const auto& display_point : current_points_) {
    const auto& point = display_point.point;
    stream << point.x << ',' << point.y << ',' << point.z << ',' << point.intensity << ','
           << point.elongation << ',' << point.velocity << ',' << point.scan_x_command << ','
           << point.scan_y_command << ','
           << (display_point.interpolated ? 1 : 0) << ','
           << static_cast<unsigned int>(display_point.temporal_observations) << '\n';
  }
  return stream.status() == QTextStream::Ok;
}

void PointCloudWidget::initializeGL() {
  initializeOpenGLFunctions();
  glClearColor(0.035F, 0.055F, 0.063F, 1.0F);
  initializeGpuRenderer();
}

void PointCloudWidget::resizeGL(int, int) {}

void PointCloudWidget::initializeGpuRenderer() {
  const bool open_gl_es = context() != nullptr && context()->isOpenGLES();
  point_program_ = std::make_unique<QOpenGLShaderProgram>();
  if (!point_program_->addShaderFromSourceCode(
          QOpenGLShader::Vertex, shaderSource(open_gl_es, kPointVertexShaderBody)) ||
      !point_program_->addShaderFromSourceCode(
          QOpenGLShader::Fragment, shaderSource(open_gl_es, kPointFragmentShaderBody)) ||
      !point_program_->link()) {
    gpu_renderer_error_ = point_program_->log();
    point_program_.reset();
    return;
  }
  if (!vertex_buffer_.create() || !vertex_array_.create()) {
    gpu_renderer_error_ = "OpenGL vertex-buffer allocation failed";
    vertex_array_.destroy();
    vertex_buffer_.destroy();
    point_program_.reset();
    return;
  }
  gpu_renderer_ready_ = true;
  vertices_dirty_ = true;
}

void PointCloudWidget::uploadVertices() {
  if (!gpu_renderer_ready_ || !vertices_dirty_ || point_program_ == nullptr) {
    return;
  }
  QOpenGLVertexArrayObject::Binder vertex_array_binder(&vertex_array_);
  if (!vertex_buffer_.bind()) {
    gpu_renderer_ready_ = false;
    gpu_renderer_error_ = "OpenGL vertex-buffer binding failed";
    return;
  }
  vertex_buffer_.allocate(vertices_.empty() ? nullptr : vertices_.data(),
                          static_cast<int>(vertices_.size() * sizeof(Vertex)));
  point_program_->bind();
  point_program_->enableAttributeArray(0);
  point_program_->setAttributeBuffer(0, GL_FLOAT, static_cast<int>(offsetof(Vertex, x)),
                                     3, static_cast<int>(sizeof(Vertex)));
  point_program_->enableAttributeArray(1);
  point_program_->setAttributeBuffer(1, GL_FLOAT, static_cast<int>(offsetof(Vertex, r)),
                                     4, static_cast<int>(sizeof(Vertex)));
  point_program_->release();
  vertex_buffer_.release();
  vertices_dirty_ = false;
}

void PointCloudWidget::drawGpuPoints() {
  if (!gpu_renderer_ready_ || point_program_ == nullptr) {
    return;
  }
  uploadVertices();
  if (!gpu_renderer_ready_ || vertices_.empty()) {
    return;
  }
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#ifdef GL_PROGRAM_POINT_SIZE
  if (context() != nullptr && !context()->isOpenGLES()) {
    glEnable(GL_PROGRAM_POINT_SIZE);
  }
#endif

  point_program_->bind();
  point_program_->setUniformValue("cloud_center", QVector3D(center_x_, center_y_, center_z_));
  point_program_->setUniformValue("cloud_extent", extent_);
  point_program_->setUniformValue("yaw_radians", static_cast<float>(yaw_degrees_ * kPi / 180.0));
  point_program_->setUniformValue("pitch_radians", static_cast<float>(pitch_degrees_ * kPi / 180.0));
  point_program_->setUniformValue("zoom_factor", zoom_);
  point_program_->setUniformValue("pan_offset", QVector2D(pan_x_, pan_y_));
  point_program_->setUniformValue("viewport_size",
                                  QVector2D(static_cast<float>(width()), static_cast<float>(height())));
  point_program_->setUniformValue("point_size", point_size_);
  {
    QOpenGLVertexArrayObject::Binder vertex_array_binder(&vertex_array_);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(vertices_.size()));
  }
  point_program_->release();

#ifdef GL_PROGRAM_POINT_SIZE
  if (context() != nullptr && !context()->isOpenGLES()) {
    glDisable(GL_PROGRAM_POINT_SIZE);
  }
#endif
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
}

void PointCloudWidget::drawPainterFallback(
    QPainter& painter, const std::function<QPointF(double, double, double)>& project) {
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
    painter.setBrush(QColor::fromRgbF(vertex.r, vertex.g, vertex.b, vertex.alpha));
    painter.drawEllipse(screen, point_radius, point_radius);
  }
}

void PointCloudWidget::paintGL() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  drawGpuPoints();

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);

  const auto yaw = static_cast<double>(yaw_degrees_) * kPi / 180.0;
  const auto pitch = static_cast<double>(pitch_degrees_) * kPi / 180.0;
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

  if (!gpu_renderer_ready_) {
    drawPainterFallback(painter, project);
  }

  const bool detections_match_frame = snapshot_ != nullptr && detections_ != nullptr &&
      detections_->last_frame_id == snapshot_->last_frame_id &&
      detections_->scan_frame_index == snapshot_->scan_frame_index;
  // Avoid a one-inference-period blank interval on every replay frame. A
  // completed result may be held briefly while the next frame is being
  // inferred. The scan-index check prevents holding boxes across a replay loop
  // or a new acquisition session, and the frame bound prevents stale boxes
  // from surviving a stalled detector.
  static constexpr std::uint64_t kMaximumDetectionHoldFrames = 12U;
  const bool detections_recent_for_frame = snapshot_ != nullptr && detections_ != nullptr &&
      detections_->last_frame_id < snapshot_->last_frame_id &&
      snapshot_->last_frame_id - detections_->last_frame_id <= kMaximumDetectionHoldFrames &&
      detections_->scan_frame_index <= snapshot_->scan_frame_index &&
      snapshot_->scan_frame_index - detections_->scan_frame_index <= kMaximumDetectionHoldFrames;
  const bool detections_visible = detections_match_frame || detections_recent_for_frame;
  if (detections_visible) {
    static constexpr std::array<std::array<int, 2>, 12> kEdges{{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};
    static constexpr std::array<std::array<float, 2>, 4> kFootprint{{
        {{1.0F, 1.0F}}, {{1.0F, -1.0F}},
        {{-1.0F, -1.0F}}, {{-1.0F, 1.0F}},
    }};

    painter.setBrush(Qt::NoBrush);
    for (const auto& box : detections_->boxes) {
      if (!std::isfinite(box.center_x_m) || !std::isfinite(box.center_y_m) ||
          !std::isfinite(box.center_z_m) || !std::isfinite(box.length_x_m) ||
          !std::isfinite(box.width_y_m) || !std::isfinite(box.height_z_m) ||
          !std::isfinite(box.yaw_rad) || !std::isfinite(box.score) ||
          box.length_x_m <= 0.0F || box.width_y_m <= 0.0F || box.height_z_m <= 0.0F) {
        continue;
      }

      const auto cosine = std::cos(static_cast<double>(box.yaw_rad));
      const auto sine = std::sin(static_cast<double>(box.yaw_rad));
      const auto half_length = static_cast<double>(box.length_x_m) * 0.5;
      const auto half_width = static_cast<double>(box.width_y_m) * 0.5;
      const auto half_height = static_cast<double>(box.height_z_m) * 0.5;
      std::array<QPointF, 8> screen_corners;
      for (std::size_t layer = 0; layer < 2; ++layer) {
        const auto z = static_cast<double>(box.center_z_m) +
            (layer == 0 ? -half_height : half_height);
        for (std::size_t corner = 0; corner < kFootprint.size(); ++corner) {
          const auto local_x = static_cast<double>(kFootprint[corner][0]) * half_length;
          const auto local_y = static_cast<double>(kFootprint[corner][1]) * half_width;
          const auto world_x = static_cast<double>(box.center_x_m) +
              local_x * cosine - local_y * sine;
          const auto world_y = static_cast<double>(box.center_y_m) +
              local_x * sine + local_y * cosine;
          screen_corners[layer * kFootprint.size() + corner] =
              projectWorld(world_x, world_y, z);
        }
      }

      const auto color = detectionColor(box.object_class);
      painter.setPen(QPen(color, 2.0));
      for (const auto& edge : kEdges) {
        painter.drawLine(screen_corners[static_cast<std::size_t>(edge[0])],
                         screen_corners[static_cast<std::size_t>(edge[1])]);
      }

      auto label_anchor = screen_corners[4];
      for (std::size_t index = 5; index < screen_corners.size(); ++index) {
        if (screen_corners[index].y() < label_anchor.y()) {
          label_anchor = screen_corners[index];
        }
      }
      const auto label = QString("%1 %2")
          .arg(QString::fromLatin1(toString(box.object_class)))
          .arg(box.score, 0, 'f', 2);
      const auto label_width = painter.fontMetrics().horizontalAdvance(label) + 10;
      const QRectF label_background(label_anchor.x(), label_anchor.y() - 22.0,
                                    label_width, 20.0);
      painter.fillRect(label_background, QColor(5, 12, 15, 210));
      painter.setPen(color);
      painter.drawText(label_background.adjusted(5.0, 0.0, -5.0, 0.0),
                       Qt::AlignLeft | Qt::AlignVCenter, label);
      painter.setBrush(Qt::NoBrush);
    }
  }

  painter.setPen(QColor("#aebdc1"));
  const auto stats = displayStats();
  auto frame_text = snapshot_
      ? QString("Frame %1 | source %2 | fused %3 | +%4 interpolated | %5 shown")
            .arg(snapshot_->scan_frame_index + 1U)
            .arg(stats.source_valid_points)
            .arg(stats.fused_points)
            .arg(stats.interpolated_points)
            .arg(stats.displayed_points)
      : QString("Waiting for complete raster frame");
  if (detections_visible) {
    frame_text += QString(" | %1 objects | %2 ms")
        .arg(detections_->boxes.size())
        .arg(detections_->timing.total_ms, 0, 'f', 1);
    if (!detections_match_frame) {
      frame_text += QString(" | updating");
    }
  }
  painter.drawText(QRect(14, 12, width() - 28, 24), Qt::AlignLeft | Qt::AlignVCenter, frame_text);
  if (axes_visible_) {
    painter.setPen(QColor("#71858b"));
    const auto renderer_text = gpu_renderer_ready_
        ? QString("X forward | Y left | Z up | meters | GPU VBO point sprites")
        : QString("X forward | Y left | Z up | meters | CPU fallback: %1")
              .arg(gpu_renderer_error_);
    painter.drawText(QRect(14, height() - 34, width() - 28, 22),
                     Qt::AlignLeft | Qt::AlignVCenter, renderer_text);
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
    pitch_degrees_ = std::clamp(pitch_degrees_ + static_cast<float>(delta.y()) * 0.45F,
                                -89.0F, 89.0F);
  } else if ((event->buttons() & (Qt::RightButton | Qt::MiddleButton)) != 0) {
    pan_x_ += static_cast<float>(delta.x()) / std::max(1, width()) * 2.0F;
    pan_y_ -= static_cast<float>(delta.y()) / std::max(1, height()) * 2.0F;
  }
  update();
}

void PointCloudWidget::wheelEvent(QWheelEvent* event) {
  zoom_ = std::clamp(zoom_ * std::pow(1.0015F, static_cast<float>(event->angleDelta().y())),
                     0.2F, 8.0F);
  update();
}

void PointCloudWidget::rebuildDisplayCloud() {
  current_points_.clear();
  const auto& display = post_processor_.displayFrame();
  current_points_.reserve(display.displayedPointCount());
  std::copy_if(display.points.begin(), display.points.end(), std::back_inserter(current_points_),
               [](const PointCloudDisplayPoint& point) { return point.point.valid; });
  rebuildVertices();
}

void PointCloudWidget::rebuildVertices() {
  vertices_.clear();
  vertices_dirty_ = true;
  if (current_points_.empty()) {
    return;
  }
  float minimum_value = std::numeric_limits<float>::max();
  float maximum_value = std::numeric_limits<float>::lowest();
  const auto valueFor = [this](const PointXYZI& point) {
    const auto distance = std::sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
    if (color_mode_ == PointCloudColorMode::Intensity && std::isfinite(point.intensity)) {
      return point.intensity;
    }
    if (color_mode_ == PointCloudColorMode::Velocity && std::isfinite(point.velocity)) {
      return point.velocity;
    }
    return distance;
  };
  for (const auto& display_point : current_points_) {
    const auto value = valueFor(display_point.point);
    minimum_value = std::min(minimum_value, value);
    maximum_value = std::max(maximum_value, value);
  }
  const auto value_span = std::max(maximum_value - minimum_value, 1.0e-6F);
  vertices_.reserve(current_points_.size());
  for (const auto& display_point : current_points_) {
    const auto& point = display_point.point;
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    colorMap((valueFor(point) - minimum_value) / value_span, red, green, blue);
    const auto alpha = display_point.interpolated ? 0.68F : 1.0F;
    vertices_.push_back({point.x, point.y, point.z, red, green, blue, alpha});
  }
}

void PointCloudWidget::fitSpatialBounds() {
  if (current_points_.empty()) {
    center_x_ = center_y_ = center_z_ = 0.0F;
    extent_ = 1.0F;
    spatial_bounds_valid_ = false;
    return;
  }
  float maximum_radius_squared = 0.0F;
  for (const auto& display_point : current_points_) {
    const auto& point = display_point.point;
    maximum_radius_squared = std::max(
        maximum_radius_squared, point.x * point.x + point.y * point.y + point.z * point.z);
  }
  center_x_ = center_y_ = center_z_ = 0.0F;
  extent_ = std::max(2.0F * std::sqrt(maximum_radius_squared), 1.0e-4F);
  spatial_bounds_valid_ = true;
}

}  // namespace fmcw
