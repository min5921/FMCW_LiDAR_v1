#include "processing/point_cloud_postprocessor.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expectNear(float actual, float expected, float tolerance, const std::string& message) {
  expect(std::abs(actual - expected) <= tolerance,
         message + " actual=" + std::to_string(actual) +
             " expected=" + std::to_string(expected));
}

fmcw::PointXYZI point(float x, float y, float z, float intensity = -20.0F,
                      float velocity = 0.0F) {
  fmcw::PointXYZI result;
  result.x = x;
  result.y = y;
  result.z = z;
  result.intensity = intensity;
  result.velocity = velocity;
  result.scan_x_command = x;
  result.scan_y_command = y;
  result.valid = true;
  return result;
}

std::shared_ptr<const fmcw::PointCloudSnapshot> snapshot(
    std::uint64_t frame_index, std::uint32_t width, std::uint32_t height,
    std::initializer_list<fmcw::PointXYZI> points,
    std::uint64_t processing_revision = 1U) {
  auto result = std::make_shared<fmcw::PointCloudSnapshot>();
  result->scan_frame_index = frame_index;
  result->processing_config_revision = processing_revision;
  result->width = width;
  result->height = height;
  result->completed_lines = height;
  result->complete = true;
  result->points.assign(points);
  return result;
}

void testTemporalFusionFillsCurrentFrameHoles() {
  fmcw::PointCloudPostProcessor processor;
  processor.setHistoryFrameCount(3U);
  processor.setVerticalInterpolationFactor(1U);

  expect(processor.push(snapshot(1U, 2U, 1U, {
      point(1.0F, 0.0F, 0.0F), point(2.0F, 0.0F, 0.0F)})),
      "first complete frame enters temporal fusion");
  expect(processor.push(snapshot(2U, 2U, 1U, {
      point(1.2F, 0.0F, 0.0F), fmcw::PointXYZI{}})),
      "second complete frame enters temporal fusion");

  const auto& display = processor.displayFrame();
  expect(display.source_valid_point_count == 1U,
         "source valid count reports only the current frame");
  expect(display.fused_point_count == 2U,
         "temporal fusion restores a current-frame invalid hole");
  expect(display.displayedPointCount() == 2U,
         "display count includes both temporally fused cells");
  expect(display.points[0].temporal_observations == 2U,
         "temporally stable cell reports both observations");
  expect(display.points[1].temporal_observations == 1U,
         "restored hole reports its single historical observation");
  expectNear(display.points[0].point.x, 1.1F, 1.0e-5F,
             "even-count temporal median averages the two center samples");
}

void testHistoryBoundAndSessionReset() {
  fmcw::PointCloudPostProcessor processor;
  processor.setHistoryFrameCount(3U);
  processor.setVerticalInterpolationFactor(1U);
  processor.push(snapshot(1U, 1U, 1U, {point(1.0F, 0.0F, 0.0F)}));
  processor.push(snapshot(2U, 1U, 1U, {fmcw::PointXYZI{}}));
  processor.push(snapshot(3U, 1U, 1U, {fmcw::PointXYZI{}}));
  processor.push(snapshot(4U, 1U, 1U, {fmcw::PointXYZI{}}));
  expect(processor.displayFrame().fused_point_count == 0U,
         "expired history cannot keep a stale point indefinitely");

  processor.push(snapshot(0U, 1U, 1U, {point(4.0F, 0.0F, 0.0F)}));
  const auto& reset_display = processor.displayFrame();
  expect(reset_display.fused_point_count == 1U &&
             reset_display.points[0].temporal_observations == 1U,
         "frame-index regression starts a fresh temporal session");
}

void testVerticalInterpolationAndEdgeGate() {
  fmcw::PointCloudPostProcessor processor;
  processor.setHistoryFrameCount(1U);
  processor.setVerticalInterpolationFactor(4U);
  processor.push(snapshot(1U, 1U, 2U, {
      point(10.0F, 0.0F, 0.10F), point(10.0F, 0.0F, -0.10F)}));

  const auto& smooth = processor.displayFrame();
  expect(smooth.display_width == 1U && smooth.display_height == 5U,
         "4x vertical interpolation expands two source rows to five display rows");
  expect(smooth.fused_point_count == 2U && smooth.interpolated_point_count == 3U,
         "smooth neighboring returns create three synthetic rows");
  expect(smooth.points[2].interpolated,
         "inserted midpoint is marked as display-only interpolation");
  expectNear(smooth.points[2].point.z, 0.0F, 1.0e-5F,
             "vertical midpoint linearly interpolates XYZ");

  processor.push(snapshot(2U, 1U, 2U, {
      point(1.0F, 0.0F, 0.0F), point(10.0F, 0.0F, 0.0F)}));
  const auto& edge = processor.displayFrame();
  expect(edge.fused_point_count == 2U && edge.interpolated_point_count == 0U,
         "large radial discontinuity is not bridged by interpolation");
}

void testTwelveLinesExpandToFortyFive() {
  fmcw::PointCloudPostProcessor processor;
  processor.setHistoryFrameCount(1U);
  processor.setVerticalInterpolationFactor(4U);

  auto input = std::make_shared<fmcw::PointCloudSnapshot>();
  input->scan_frame_index = 1U;
  input->processing_config_revision = 1U;
  input->width = 1U;
  input->height = 12U;
  input->completed_lines = 12U;
  input->complete = true;
  input->points.reserve(12U);
  for (std::uint32_t row = 0; row < 12U; ++row) {
    input->points.push_back(point(3.0F, 0.0F, static_cast<float>(row) * 0.02F));
  }

  expect(processor.push(input), "12-line frame enters display post-processing");
  const auto& display = processor.displayFrame();
  expect(display.display_width == 1U && display.display_height == 45U,
         "4x interpolation expands 12 source rows to 45 display rows");
  expect(display.displayedPointCount() == 45U,
         "smooth 12-line column produces 45 displayed points");
  expect(display.interpolated_point_count == 33U,
         "12-line expansion creates 33 display-only points");
}

void testRejectsMalformedSnapshot() {
  fmcw::PointCloudPostProcessor processor;
  auto malformed = std::make_shared<fmcw::PointCloudSnapshot>();
  malformed->complete = true;
  malformed->width = 2U;
  malformed->height = 2U;
  malformed->points.resize(3U);
  expect(!processor.push(malformed),
         "post-processor rejects a cloud whose organized dimensions do not match");
  expect(processor.displayFrame().points.empty(),
         "malformed input leaves the display state unchanged");
}

}  // namespace

int main() {
  testTemporalFusionFillsCurrentFrameHoles();
  testHistoryBoundAndSessionReset();
  testVerticalInterpolationAndEdgeGate();
  testTwelveLinesExpandToFortyFive();
  testRejectsMalformedSnapshot();
  if (failures == 0) {
    std::cout << "All point-cloud post-processing tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
