#include "detection/centerpoint_input_builder.h"
#include "detection/detection_types.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expectNear(float actual, float expected, float tolerance,
                const std::string& message) {
  expect(std::abs(actual - expected) <= tolerance, message);
}

fmcw::PointXYZI point(float x, float y, float z, float intensity, float velocity) {
  fmcw::PointXYZI value;
  value.x = x;
  value.y = y;
  value.z = z;
  value.intensity = intensity;
  value.velocity = velocity;
  value.valid = true;
  return value;
}

void testInputContract() {
  fmcw::PointCloudSnapshot snapshot;
  snapshot.last_frame_id = 17;
  snapshot.scan_frame_index = 4;
  snapshot.processing_config_revision = 9;
  snapshot.complete = true;
  snapshot.points.push_back(point(10.0F, 2.0F, 1.0F, -50.0F, 3.5F));
  snapshot.points.push_back(point(80.0F, 0.0F, 0.0F, -20.0F, 1.0F));
  snapshot.points.emplace_back();

  const auto input = fmcw::buildCenterPointInput(snapshot);
  expect(input.last_frame_id == 17, "frame id is preserved");
  expect(input.scan_frame_index == 4, "scan frame index is preserved");
  expect(input.processing_config_revision == 9, "processing revision is preserved");
  expect(input.source_frame_complete, "complete source state is preserved");
  expect(input.source_points == 3, "source point count is recorded");
  expect(input.pointCount() == 1, "only valid in-range points are accepted");
  expect(input.discarded_out_of_range_points == 1, "out-of-range point is counted");
  expect(input.discarded_invalid_points == 1, "invalid point is counted");
  expect(input.features.size() == 5, "CenterPoint input uses five features");
  expectNear(input.features[0], 10.0F, 1.0e-6F, "+X remains forward");
  expectNear(input.features[1], 2.0F, 1.0e-6F, "+Y remains left");
  expectNear(input.features[2], 1.0F, 1.0e-6F, "+Z remains up");
  expectNear(input.features[3], 0.5F, 1.0e-6F, "dB intensity uses fixed normalization");
  expectNear(input.features[4], 0.0F, 1.0e-6F, "novelocity weight input uses zero fifth feature");
}

void testVelocityFeatureCanBeEnabledLater() {
  fmcw::PointCloudSnapshot snapshot;
  snapshot.complete = true;
  snapshot.points.push_back(point(5.0F, -1.0F, 0.2F, 10.0F, -4.25F));

  fmcw::CenterPointInputConfig config;
  config.fifth_feature = fmcw::CenterPointFifthFeature::Velocity;
  const auto input = fmcw::buildCenterPointInput(snapshot, config);
  expect(input.pointCount() == 1, "velocity input point is accepted");
  expectNear(input.features[3], 1.0F, 1.0e-6F, "fixed intensity normalization clamps high values");
  expectNear(input.features[4], -4.25F, 1.0e-6F, "velocity can become the fifth feature");
}

void testBoxClassNames() {
  expect(std::string(fmcw::toString(fmcw::ObjectClass::Vehicle)) == "Vehicle",
         "vehicle class name is stable");
  expect(std::string(fmcw::toString(fmcw::ObjectClass::Pedestrian)) == "Pedestrian",
         "pedestrian class name is stable");
  expect(std::string(fmcw::toString(fmcw::ObjectClass::Cyclist)) == "Cyclist",
         "cyclist class name is stable");
}

}  // namespace

int main() {
  testInputContract();
  testVelocityFeatureCanBeEnabledLater();
  testBoxClassNames();
  if (failures != 0) {
    std::cerr << failures << " detection input test(s) failed\n";
    return 1;
  }
  std::cout << "Detection input contract tests passed\n";
  return 0;
}
