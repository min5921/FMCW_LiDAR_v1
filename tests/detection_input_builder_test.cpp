#include "detection/centerpoint_input_builder.h"
#include "detection/detection_types.h"
#include "detection/object_detection_policy.h"
#include "detection/object_detection_service.h"
#include "detection/object_detector.h"
#include "detection/point_cloud_file_loader.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

void testWaymoFeatureContract() {
  fmcw::PointCloudSnapshot snapshot;
  snapshot.complete = true;
  snapshot.feature_encoding = fmcw::PointCloudFeatureEncoding::CenterPointWaymo;
  auto value = point(5.0F, -1.0F, 0.2F, 0.625F, 0.0F);
  value.elongation = 0.375F;
  snapshot.points.push_back(value);

  const auto input = fmcw::buildCenterPointInput(snapshot);
  expect(input.pointCount() == 1, "Waymo point is accepted");
  expectNear(input.features[3], 0.625F, 1.0e-6F,
             "preprocessed Waymo intensity is preserved");
  expectNear(input.features[4], 0.375F, 1.0e-6F,
             "Waymo elongation is preserved as the fifth feature");
}

void testPointCloudFileLoading() {
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
      ("fmcw_point_cloud_import_" + std::to_string(unique));
  std::filesystem::create_directories(root);

  const std::array<float, 10> binary_points{
      1.0F, 2.0F, 3.0F, 0.25F, 0.5F,
      4.0F, 5.0F, 1.0F, 0.75F, 0.125F};
  const auto binary_path = root / "points.bin";
  {
    std::ofstream stream(binary_path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(binary_points.data()),
                 static_cast<std::streamsize>(sizeof(binary_points)));
  }
  fmcw::PointCloudFileLoadResult loaded;
  std::string error;
  expect(fmcw::loadCenterPointCloudFile(binary_path, 7U, loaded, error),
         "CenterPoint Nx5 binary loads: " + error);
  expect(loaded.snapshot != nullptr && loaded.snapshot->points.size() == 2U,
         "binary loader publishes both points");
  expect(loaded.snapshot != nullptr &&
             loaded.snapshot->feature_encoding ==
                 fmcw::PointCloudFeatureEncoding::CenterPointWaymo,
         "binary loader marks the Waymo feature encoding");
  if (loaded.snapshot != nullptr && loaded.snapshot->points.size() == 2U) {
    expectNear(loaded.snapshot->points[1].elongation, 0.125F, 1.0e-6F,
               "binary loader preserves elongation");
  }

  const auto pcd_path = root / "sample.pcd";
  {
    std::ofstream stream(pcd_path);
    stream << "# .PCD v0.7\n"
              "VERSION 0.7\n"
              "FIELDS x y z intensity elongation\n"
              "SIZE 4 4 4 4 4\n"
              "TYPE F F F F F\n"
              "COUNT 1 1 1 1 1\n"
              "WIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA ascii\n"
              "6 7 8 0.9 0.4\n";
  }
  loaded = {};
  error.clear();
  expect(fmcw::loadCenterPointCloudFile(pcd_path, 8U, loaded, error),
         "ASCII PCD loads: " + error);
  expect(loaded.snapshot != nullptr && loaded.snapshot->points.size() == 1U,
         "PCD loader publishes one point");
  if (loaded.snapshot != nullptr && !loaded.snapshot->points.empty()) {
    expectNear(loaded.snapshot->points[0].intensity, 0.9F, 1.0e-6F,
               "PCD loader preserves model-ready intensity");
    expectNear(loaded.snapshot->points[0].elongation, 0.4F, 1.0e-6F,
               "PCD loader preserves elongation");
  }

  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  expect(!cleanup_error, "temporary point-cloud import files are removed");
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

bool referenceInferencePaths(std::filesystem::path& weights_root,
                             std::filesystem::path& points_bin) {
#ifdef _WIN32
  const wchar_t* weights = _wgetenv(L"FMCW_CENTERPOINT_TEST_WEIGHTS_ROOT");
  const wchar_t* points = _wgetenv(L"FMCW_CENTERPOINT_TEST_POINTS_BIN");
#else
  const char* weights = std::getenv("FMCW_CENTERPOINT_TEST_WEIGHTS_ROOT");
  const char* points = std::getenv("FMCW_CENTERPOINT_TEST_POINTS_BIN");
#endif
  if (weights == nullptr || points == nullptr ||
      weights[0] == 0 || points[0] == 0) {
    return false;
  }
  weights_root = std::filesystem::path(weights);
  points_bin = std::filesystem::path(points);
  return true;
}

void testReferenceGpuInferenceWhenConfigured() {
  std::filesystem::path weights_root;
  std::filesystem::path points_bin;
  if (!referenceInferencePaths(weights_root, points_bin)) {
    std::cout << "Reference GPU inference skipped (sample paths are not configured)\n";
    return;
  }
  if (!fmcw::centerPointBackendCompiled()) {
    std::cout << "Reference GPU inference skipped (CenterPoint is not compiled)\n";
    return;
  }

  std::ifstream stream(points_bin, std::ios::binary | std::ios::ate);
  expect(stream.is_open(), "reference points.bin can be opened");
  if (!stream.is_open()) {
    return;
  }
  const auto byte_count = static_cast<std::streamoff>(stream.tellg());
  expect(byte_count > 0, "reference points.bin is not empty");
  expect(byte_count % static_cast<std::streamoff>(sizeof(float) * 5) == 0,
         "reference points.bin contains five floats per point");
  if (byte_count <= 0 ||
      byte_count % static_cast<std::streamoff>(sizeof(float) * 5) != 0) {
    return;
  }

  std::vector<float> features(
      static_cast<std::size_t>(byte_count) / sizeof(float));
  stream.seekg(0, std::ios::beg);
  stream.read(reinterpret_cast<char*>(features.data()),
              static_cast<std::streamsize>(byte_count));
  expect(stream.good(), "reference points.bin is read completely");
  if (!stream.good()) {
    return;
  }

  auto detector = fmcw::createCenterPointObjectDetector();
  expect(detector != nullptr, "compiled CenterPoint detector can be created");
  if (detector == nullptr) {
    return;
  }
  fmcw::ObjectDetectorConfig config;
  config.weights_root = weights_root;
  std::string error;
  expect(detector->initialize(config, error),
         "CenterPoint initializes with reference weights: " + error);
  if (!detector->ready()) {
    return;
  }

  fmcw::CenterPointInput input;
  input.last_frame_id = 1;
  input.scan_frame_index = 1;
  input.processing_config_revision = 1;
  input.source_frame_complete = true;
  input.source_points = features.size() / 5U;
  input.features = std::move(features);
  const auto result = detector->detect(input);
  expect(result.backend_ready, "reference inference keeps the backend ready");
  expect(result.status == "ok", "reference inference completes: " + result.status);
  expect(!result.boxes.empty(), "reference inference produces object boxes");
  std::cout << "Reference GPU inference: " << result.accepted_points << " points, "
            << result.boxes.size() << " boxes, " << result.timing.total_ms
            << " ms total\n";
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
  testWaymoFeatureContract();
  testPointCloudFileLoading();
  testBoxClassNames();
  testDetectorConfigurationValidation();
  testReferenceGpuInferenceWhenConfigured();
  testObjectDetectionGate();
  testObjectDetectionService();
  if (failures != 0) {
    std::cerr << failures << " detection input test(s) failed\n";
    return 1;
  }
  std::cout << "Detection input contract tests passed\n";
  return 0;
}
