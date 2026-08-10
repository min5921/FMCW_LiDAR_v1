#pragma once

#include "processing/point_cloud_postprocessor.h"

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

class QPainter;
class QPointF;

namespace fmcw {

enum class PointCloudColorMode {
  Intensity,
  Velocity,
  Distance,
};

struct PointCloudDisplayStats {
  std::size_t source_valid_points = 0U;
  std::size_t fused_points = 0U;
  std::size_t interpolated_points = 0U;
  std::size_t displayed_points = 0U;
  std::uint32_t source_height = 0U;
  std::uint32_t display_height = 0U;
};

class PointCloudWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
 public:
  explicit PointCloudWidget(QWidget* parent = nullptr);
  ~PointCloudWidget() override;

  void setSnapshot(std::shared_ptr<const PointCloudSnapshot> snapshot);
  void setColorMode(PointCloudColorMode mode);
  void setPointSize(float pixels);
  void setAxesVisible(bool visible);
  void setTemporalFusionFrames(std::uint32_t frame_count);
  void setVerticalInterpolationFactor(std::uint32_t factor);
  PointCloudDisplayStats displayStats() const;
  void resetCamera();
  bool saveCurrentCloud(const QString& path) const;

 protected:
  void initializeGL() override;
  void resizeGL(int width, int height) override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  struct Vertex {
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
    float alpha;
  };

  void rebuildDisplayCloud();
  void rebuildVertices();
  void fitSpatialBounds();
  void initializeGpuRenderer();
  void uploadVertices();
  void drawGpuPoints();
  void drawPainterFallback(QPainter& painter, const std::function<QPointF(double, double, double)>& project);

  std::shared_ptr<const PointCloudSnapshot> snapshot_;
  PointCloudPostProcessor post_processor_;
  std::vector<PointCloudDisplayPoint> current_points_;
  std::vector<Vertex> vertices_;
  std::unique_ptr<QOpenGLShaderProgram> point_program_;
  QOpenGLBuffer vertex_buffer_{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject vertex_array_;
  QString gpu_renderer_error_;
  QPoint last_mouse_position_;
  PointCloudColorMode color_mode_ = PointCloudColorMode::Intensity;
  float point_size_ = 3.0F;
  float yaw_degrees_ = -35.0F;
  float pitch_degrees_ = -20.0F;
  float zoom_ = 1.0F;
  float pan_x_ = 0.0F;
  float pan_y_ = 0.0F;
  float center_x_ = 0.0F;
  float center_y_ = 0.0F;
  float center_z_ = 0.0F;
  float extent_ = 1.0F;
  bool axes_visible_ = true;
  bool spatial_bounds_valid_ = false;
  bool gpu_renderer_ready_ = false;
  bool vertices_dirty_ = true;
};

}  // namespace fmcw
