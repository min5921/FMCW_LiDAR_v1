#include "detection/centerpoint_input_builder.h"
#include "detection/detection_types.h"
#include "detection/object_detection_policy.h"
#include "detection/object_detection_service.h"
#include "detection/object_detector.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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

void testDetectorConfigurationValidation() {
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
      ("fmcw_centerpoint_config_" + std::to_string(unique));
  std::filesystem::create_directories(root / "04_pfn");
  std::filesystem::create_directories(root / "06_rpn");
  std::filesystem::create_directories(root / "07_head");
  std::ofstream(root / "04_pfn" / "weights_metadata.json") << "{}\n";
  std::ofstream(root / "06_rpn" / "rpn_weights_metadata.json") << "{}\n";
  std::ofstream(root / "07_head" / "head_weights_metadata.json") << "{}\n";

  fmcw::ObjectDetectorConfig config;
  config.weights_root = root;
  std::string error;
  expect(fmcw::validateObjectDetectorConfig(config, error),
         "complete CenterPoint weight layout is accepted: " + error);
  config.maximum_detections = 501;
  expect(!fmcw::validateObjectDetectorConfig(config, error),
         "maximum detection count above the CUDA limit is rejected");

  const auto compiled = fmcw::centerPointBackendCompiled();
  const auto detector = fmcw::createCenterPointObjectDetector();
  expect(compiled == (detector != nullptr),
         "CenterPoint factory matches the compiled backend flag");

  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  expect(!cleanup_error, "temporary CenterPoint validation files are removed");
}

class FakeObjectDetector final : public fmcw::ObjectDetector {
 public:
  const char* backendName() const override { return "fake detector"; }

  bool initialize(const fmcw::ObjectDetectorConfig&, std::string& error) override {
    ready_ = true;
    error.clear();
    return true;
  }

  bool ready() const override { return ready_; }

  fmcw::DetectionSnapshot detect(const fmcw::CenterPointInput& input) override {
    fmcw::DetectionSnapshot snapshot;
    snapshot.last_frame_id = input.last_frame_id;
    snapshot.scan_frame_index = input.scan_frame_index;
    snapshot.processing_config_revision = input.processing_config_revision;
    snapshot.source_frame_complete = input.source_frame_complete;
    snapshot.backend_ready = true;
    snapshot.status = "ok";
    return snapshot;
  }

 private:
  bool ready_ = false;
};

void testObjectDetectionGate() {
  auto gate = fmcw::evaluateObjectDetectionGate(
      false, fmcw::FftBackendKind::Cuda, true, true);
  expect(!gate.can_enable, "object detection is blocked before configuration is applied");

  gate = fmcw::evaluateObjectDetectionGate(
      true, fmcw::FftBackendKind::Fftw, true, true);
  expect(!gate.can_enable, "object detection is blocked for FFTW processing");

  gate = fmcw::evaluateObjectDetectionGate(
      true, fmcw::FftBackendKind::Cuda, true, true);
  expect(gate.can_enable, "object detection is allowed for CUDA cuFFT processing");

  gate = fmcw::evaluateObjectDetectionGate(
      true, fmcw::FftBackendKind::Cuda, false, true);
  expect(!gate.can_enable, "object detection is blocked when CenterPoint is not compiled");

  gate = fmcw::evaluateObjectDetectionGate(
      true, fmcw::FftBackendKind::Cuda, true, false);
  expect(!gate.can_enable, "object detection is blocked without a CUDA runtime device");
}

void testObjectDetectionService() {
  fmcw::ObjectDetectionService service(std::make_unique<FakeObjectDetector>());
  fmcw::ObjectDetectorConfig config;
  std::string error;
  expect(service.initialize(config, error), "fake detection service initializes: " + error);
  expect(service.start(error), "fake detection service starts: " + error);

  auto incomplete = std::make_shared<fmcw::PointCloudSnapshot>();
  expect(service.enqueue(incomplete) == fmcw::DetectionEnqueueResult::InvalidFrame,
         "incomplete point-cloud frame is rejected");

  auto complete = std::make_shared<fmcw::PointCloudSnapshot>();
  complete->last_frame_id = 41;
  complete->scan_frame_index = 7;
  complete->processing_config_revision = 3;
  complete->complete = true;
  fmcw::PointXYZI point;
  point.valid = true;
  complete->points.push_back(point);
  expect(service.enqueue(complete) == fmcw::DetectionEnqueueResult::Accepted,
         "complete point-cloud frame enters the asynchronous detector");

  for (int attempt = 0; attempt < 100 && service.latestSnapshot() == nullptr; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const auto result = service.latestSnapshot();
  expect(result != nullptr && result->scan_frame_index == 7,
         "asynchronous detector publishes the matching scan frame");
  service.stop();
  expect(!service.status().running, "detection service stops cleanly");
}

}  // namespace

int main() {
  testInputContract();
  testVelocityFeatureCanBeEnabledLater();
  testBoxClassNames();
  testDetectorConfigurationValidation();
  testObjectDetectionGate();
  testObjectDetectionService();
  if (failures != 0) {
    std::cerr << failures << " detection input test(s) failed\n";
    return 1;
  }
  std::cout << "Detection input contract tests passed\n";
  return 0;
}
