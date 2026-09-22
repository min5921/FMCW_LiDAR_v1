#include "ui/point_cloud/point_cloud_camera.h"
#include "ui/point_cloud/point_cloud_grid.h"

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

    for (float zoom : {1.0e-6F, 0.01F, 0.2F, 1.0F, 8.0F, 200.0F, 1.0e6F}) {
      camera.zoom = zoom;
      const fmcw::PointCloudProjection projection(camera, {640, 480}, 100.0F);
      check(projection.projectPoint({0, -100, 0}, screen, depth),
            "A distant visible point must survive at every zoom setting");
      check(depth < 1.0F, "Visible geometry must not equal the cleared depth value");
      check(projection.projectPoint({0, 1, 0}, screen, depth),
            "Zoom must not drive the eye through a point in front of the origin");
      check(projection.projectPoint({0.1F / zoom, 0, 0}, screen, depth) &&
                std::abs(screen.x() - (320.0 - 480.0 * 0.72 * 0.1 / 3.4)) < 0.001,
            "Magnification must remain proportional across the extended zoom range");
      if (zoom >= 1.0F) {
        check(projection.projectSegment({-100, 0, 0}, {100, 0, 0}, line) &&
                  std::abs(line.length() - 640.0) < 0.1,
              "A grid line must span the viewport without collapsing at extreme zoom");
      }
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
    camera.zoom = 1.0F;
    camera.zoomBy(6000);
    check(camera.zoom > 200.0F, "Wheel zoom must pass the former upper limit");
    camera.zoomBy(-12000);
    check(camera.zoom < 0.01F, "Wheel zoom must pass the former lower limit");
    camera.zoomBy(6000);
    check(std::abs(camera.zoom - 1.0F) < 0.001F, "Opposite wheel deltas must restore magnification");
    camera.zoomBy(1000000);
    check(std::isfinite(camera.zoom) && camera.zoom > 200, "Large wheel input must remain finite");
    camera.zoomBy(-1000000);
    check(camera.zoom > 0.0F && camera.zoom < 0.01F, "Large negative wheel input must remain positive");

    for (const auto viewport : {QSize(640, 480), QSize(960, 320), QSize(480, 960)}) {
      for (const auto point : {QVector3D(0.4F, -0.2F, 0.3F), QVector3D(-0.3F, 0.1F, -0.2F)}) {
        fmcw::PointCloudCamera anchored;
        anchored.pan_x = 0.25F;
        anchored.pan_y = -0.1F;
        const auto original = anchored;
        QPointF cursor;
        check(fmcw::PointCloudProjection(anchored, viewport, 1).projectPoint(point, cursor, depth),
              "Cursor zoom fixture must start inside the viewport");
        for (const float delta : {120.0F, 720.0F, -240.0F, -600.0F}) {
          anchored.zoomAt(delta, cursor, viewport);
          check(fmcw::PointCloudProjection(anchored, viewport, 1).projectPoint(point, screen, depth) &&
                    QLineF(cursor, screen).length() < 0.01,
                "The point under the pointer must stay fixed while zooming after a pan");
        }
        check(std::abs(anchored.zoom - original.zoom) < 0.001F &&
                  std::abs(anchored.pan_x - original.pan_x) < 0.001F &&
                  std::abs(anchored.pan_y - original.pan_y) < 0.001F,
              "Opposite cursor zoom steps must restore both pan and magnification");
      }
    }
    for (const float limit : {1.0e-6F, 1.0e6F}) {
      fmcw::PointCloudCamera anchored;
      anchored.zoom = limit;
      anchored.pan_x = 0.4F;
      anchored.pan_y = -0.3F;
      anchored.zoomAt(limit < 1 ? -120 : 120, {530, 140}, {640, 480});
      check(anchored.pan_x == 0.4F && anchored.pan_y == -0.3F,
            "Continuing to scroll at a zoom guard must not shift the view");
    }

    for (float radius : {0.01F, 3.2F, 25.0F, 100000.0F}) {
      const fmcw::PointCloudGrid grid(radius, 1.0F);
      check(grid.coordinate(1) == 1.0F && grid.coordinate(-5) == -5.0F,
            "One grid cell must remain one world meter after fitting a different cloud");
      check(grid.half_cells <= fmcw::PointCloudGrid::maximum_half_cells,
            "Large clouds must not allocate an unbounded grid");
    }
    const fmcw::PointCloudGrid fine_grid(10.0F, 0.1F);
    check(std::abs(fine_grid.coordinate(5) - 0.5F) < 1.0e-6F,
          "Explicit grid spacing must use meters rather than normalized camera units");
    std::cout << "Camera clipping, fixed framing, resize, extended zoom and metric grid passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
