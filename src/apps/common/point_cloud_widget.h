#pragma once

#include "processing/processing_snapshots.h"

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPoint>

#include <memory>
#include <vector>

namespace fmcw {

enum class PointCloudColorMode {
  Intensity,
  Velocity,
  Distance,
};

class PointCloudWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
 public:
  explicit PointCloudWidget(QWidget* parent = nullptr);
  ~PointCloudWidget() override;

  void setSnapshot(std::shared_ptr<const PointCloudSnapshot> snapshot);
  void setColorMode(PointCloudColorMode mode);
  void setPointSize(float pixels);
  void setAxesVisible(bool visible);
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
  };

  void rebuildVertices();
  void fitSpatialBounds();

  std::shared_ptr<const PointCloudSnapshot> snapshot_;
  std::vector<PointXYZI> current_points_;
  std::vector<Vertex> vertices_;
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
};

}  // namespace fmcw
