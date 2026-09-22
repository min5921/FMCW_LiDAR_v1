#include "ui/point_cloud/point_cloud_camera.h"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  try {
    fmcw::PointCloudCamera camera;
    camera.yaw_degrees = camera.pitch_degrees = 0.0F;
    QPointF screen;
    float depth = 0.0F;
    const fmcw::PointCloudProjection normal(camera, {640, 480}, 1.0F);
    check(normal.projectPoint({1, 0, 0}, screen, depth) && screen.x() < 320,
          "A right-handed camera on +Y must see +X on its left");
    check(normal.projectPoint({0, 0, 1}, screen, depth) && screen.y() < 240,
          "+Z must project upwards");
    check(!normal.projectPoint({0, 4, 0}, screen, depth), "Behind-camera point must be clipped");
    check(!normal.projectPoint({0, 3.395F, 0}, screen, depth), "Near-plane point must be clipped");
    check(normal.projectPoint({0, 3.38F, 0}, screen, depth), "Point beyond the near plane must survive");
    QLineF line;
    check(normal.projectSegment({0.1F, 0, 0}, {0.1F, 4, 0}, line),
          "A line crossing the eye must retain its visible portion");
    for (const auto endpoint : {line.p1(), line.p2()}) {
      check(std::isfinite(endpoint.x()) && std::isfinite(endpoint.y()) &&
                endpoint.x() >= -0.1 && endpoint.x() <= 640.1 &&
                endpoint.y() >= -0.1 && endpoint.y() <= 480.1,
            "Clipped line must stay within the viewport");
    }
    check(!normal.projectSegment({0, 4, 0}, {1, 5, 0}, line), "Behind-eye line must be hidden");

    for (float zoom : {0.01F, 0.2F, 1.0F, 8.0F, 200.0F}) {
      camera.zoom = zoom;
      const fmcw::PointCloudProjection projection(camera, {640, 480}, 100.0F);
      check(projection.projectPoint({0, -100, 0}, screen, depth),
            "A distant visible point must survive at every zoom setting");
      check(depth < 1.0F, "Visible geometry must not equal the cleared depth value");
    }
    camera.zoom = 1.0F;
    QPointF old_screen;
    normal.projectPoint({0.2F, 0, 0.1F}, old_screen, depth);
    const fmcw::PointCloudProjection later(camera, {640, 480}, 100.0F);
    check(later.projectPoint({0.2F, 0, 0.1F}, screen, depth) &&
              QLineF(old_screen, screen).length() < 0.001,
          "Changing clipping bounds must not move or rescale the camera");
    for (const auto size : {QSize(640, 480), QSize(960, 320), QSize(480, 960)}) {
      const fmcw::PointCloudProjection projection(camera, size, 1.0F);
      QPointF left, right, top, bottom;
      projection.projectPoint({-0.2F, 0, 0}, left, depth);
      projection.projectPoint({0.2F, 0, 0}, right, depth);
      projection.projectPoint({0, 0, 0.2F}, top, depth);
      projection.projectPoint({0, 0, -0.2F}, bottom, depth);
      check(std::abs(QLineF(left, right).length() - QLineF(top, bottom).length()) < 0.001,
            "Resizing must preserve equal horizontal and vertical scales");
    }
    for (float yaw : {-180.0F, -35.0F, 0.0F, 90.0F, 180.0F}) {
      for (float pitch : {-89.0F, 0.0F, 20.0F, 89.0F}) {
        camera.yaw_degrees = yaw;
        camera.pitch_degrees = pitch;
        const fmcw::PointCloudProjection projection(camera, {640, 480}, 1.0F);
        check(projection.projectPoint({}, screen, depth) &&
                  QLineF(screen, {320, 240}).length() < 0.01,
              "Orbit rotation must keep the origin fixed");
      }
    }
    camera.zoomBy(1000000);
    check(std::isfinite(camera.zoom) && camera.zoom <= 200.001F, "Zoom must remain finite");
    camera.zoomBy(-1000000);
    check(camera.zoom >= 0.00999F, "Zoom must remain positive");
    std::cout << "Camera handedness, near/far and line clipping, fixed framing, resize and zoom passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
