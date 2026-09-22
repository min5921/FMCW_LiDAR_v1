#include "ui/point_cloud/point_cloud_widget.h"

#include <QFile>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QSurfaceFormat>
#include <QTextStream>
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

constexpr auto kPointVertexShaderBody = R"glsl(
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;

uniform vec3 cloud_center;
uniform float cloud_extent;
uniform mat4 view_projection;
uniform float point_size;

out vec4 vertex_color;

void main() {
  vec3 point = (in_position - cloud_center) * (2.0 / max(cloud_extent, 1.0e-6));
  gl_Position = view_projection * vec4(point, 1.0);
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

}  // namespace

PointCloudWidget::PointCloudWidget(QWidget* parent, PointCloudRenderer renderer)
    : QOpenGLWidget(parent), renderer_(renderer) {
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
  rebuildDisplayCloud();
  if (!spatial_bounds_valid_) {
    fitSpatialBounds();
  }
  update();
}

void PointCloudWidget::clearSnapshot(bool reset_camera) {
  snapshot_.reset();
  post_processor_.reset();
  current_points_.clear();
  vertices_.clear();
  vertices_dirty_ = true;
  display_radius_ = 0.0;
  if (reset_camera) {
    camera_ = PointCloudCamera{};
    spatial_bounds_valid_ = false;
    center_x_ = center_y_ = center_z_ = 0.0F;
    extent_ = 1.0F;
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
  camera_ = PointCloudCamera{};
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
  if (renderer_ == PointCloudRenderer::Automatic) {
    GLfloat point_size_range[2] = {1.0F, 1.0F};
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, point_size_range);
    maximum_point_size_ = point_size_range[1];
    initializeGpuRenderer();
  } else {
    gpu_renderer_error_ = "Painter renderer selected";
  }
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

void PointCloudWidget::drawGpuPoints(const PointCloudProjection& projection) {
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
  point_program_->setUniformValue("view_projection", projection.matrix());
  point_program_->setUniformValue("point_size",
      std::min(point_size_ * static_cast<float>(devicePixelRatioF()), maximum_point_size_));
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
    QPainter& painter, const PointCloudProjection& projection) {
  painter.setPen(Qt::NoPen);
  const auto point_radius = static_cast<double>(point_size_) * 0.5;
  struct ProjectedPoint {
    const Vertex* vertex;
    QPointF screen;
    float depth;
  };
  std::vector<ProjectedPoint> projected;
  projected.reserve(vertices_.size());
  for (const auto& vertex : vertices_) {
    ProjectedPoint point{&vertex, {}, 0.0F};
    const auto normalized = QVector3D(vertex.x - center_x_, vertex.y - center_y_,
                                      vertex.z - center_z_) * (2.0F / extent_);
    if (projection.projectPoint(normalized, point.screen, point.depth)) {
      projected.push_back(point);
    }
  }
  std::stable_sort(projected.begin(), projected.end(), [](const auto& a, const auto& b) {
    return a.depth > b.depth;
  });
  for (const auto& point : projected) {
    const auto& vertex = *point.vertex;
    painter.setBrush(QColor::fromRgbF(vertex.r, vertex.g, vertex.b, vertex.alpha));
    painter.drawEllipse(point.screen, point_radius, point_radius);
  }
}

void PointCloudWidget::paintGL() {
  const PointCloudProjection projection(camera_, size(),
      static_cast<float>(2.0 * display_radius_ / extent_));
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  drawGpuPoints(projection);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);

  const auto normalize = [&](const QVector3D& point) {
    return (point - QVector3D(center_x_, center_y_, center_z_)) * (2.0F / extent_);
  };
  const auto drawWorldLine = [&](const QVector3D& from, const QVector3D& to) {
    QLineF line;
    if (projection.projectSegment(normalize(from), normalize(to), line)) {
      painter.drawLine(line);
    }
  };

  painter.setPen(QPen(QColor(52, 72, 78, 120), 1.0));
  for (int index = -4; index <= 4; ++index) {
    const auto coordinate = static_cast<float>(index) / 4.0F;
    const auto x = center_x_ + coordinate * extent_ * 0.5F;
    const auto y = center_y_ + coordinate * extent_ * 0.5F;
    drawWorldLine({x, center_y_ - extent_ * 0.5F, 0.0F},
                  {x, center_y_ + extent_ * 0.5F, 0.0F});
    drawWorldLine({center_x_ - extent_ * 0.5F, y, 0.0F},
                  {center_x_ + extent_ * 0.5F, y, 0.0F});
  }

  if (axes_visible_) {
    const auto axis_length = extent_ * 0.325F;
    const auto drawAxis = [&](const QVector3D& endpoint, const QColor& color, const char* label) {
      painter.setPen(QPen(color, 2.0));
      drawWorldLine({}, endpoint);
      QPointF screen;
      float depth = 0.0F;
      if (projection.projectPoint(normalize(endpoint), screen, depth)) {
        painter.drawText(screen + QPointF(4.0, 0.0), label);
      }
    };
    drawAxis({axis_length, 0.0F, 0.0F}, QColor("#e05a67"), "X");
    drawAxis({0.0F, axis_length, 0.0F}, QColor("#67c98c"), "Y");
    drawAxis({0.0F, 0.0F, axis_length}, QColor("#55aee6"), "Z");
  }

  if (!gpu_renderer_ready_) {
    drawPainterFallback(painter, projection);
  }

  painter.setPen(QColor("#aebdc1"));
  const auto stats = displayStats();
  auto frame_text = snapshot_
      ? QString("Frame %1 | source %2 | fused %3 | +%4 interpolated | %5 display points")
            .arg(snapshot_->scan_frame_index + 1U)
            .arg(stats.source_valid_points)
            .arg(stats.fused_points)
            .arg(stats.interpolated_points)
            .arg(stats.displayed_points)
      : QString("Waiting for complete raster frame");
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
    camera_.yaw_degrees = std::remainder(camera_.yaw_degrees - static_cast<float>(delta.x()) * 0.45F, 360.0F);
    camera_.pitch_degrees = std::clamp(camera_.pitch_degrees + static_cast<float>(delta.y()) * 0.45F,
                                -89.0F, 89.0F);
  } else if ((event->buttons() & (Qt::RightButton | Qt::MiddleButton)) != 0) {
    camera_.pan_x += static_cast<float>(delta.x()) / std::max(1, width()) * 2.0F;
    camera_.pan_y -= static_cast<float>(delta.y()) / std::max(1, height()) * 2.0F;
  }
  update();
}

void PointCloudWidget::wheelEvent(QWheelEvent* event) {
  camera_.zoomBy(static_cast<float>(event->angleDelta().y()));
  event->accept();
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
  display_radius_ = 0.0;
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
    display_radius_ = std::max(display_radius_, std::hypot(
        static_cast<double>(point.x), static_cast<double>(point.y), static_cast<double>(point.z)));
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
  center_x_ = center_y_ = center_z_ = 0.0F;
  extent_ = static_cast<float>(std::clamp(2.0 * display_radius_, 1.0e-4,
                                        static_cast<double>(std::numeric_limits<float>::max())));
  spatial_bounds_valid_ = true;
}

}  // namespace fmcw
