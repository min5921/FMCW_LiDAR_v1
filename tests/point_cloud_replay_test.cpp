#include "storage/binary_storage.h"
#include "storage/point_cloud_replay.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

std::filesystem::path testDirectory() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path = std::filesystem::temp_directory_path() /
      ("fmcw_point_cloud_replay_" + std::to_string(nonce));
  std::filesystem::create_directories(path);
  return path;
}

fmcw::PointXYZI point(float x, float y, float z, float intensity, float velocity) {
  fmcw::PointXYZI result;
  result.x = x;
  result.y = y;
  result.z = z;
  result.intensity = intensity;
  result.velocity = velocity;
  result.valid = true;
  return result;
}

fmcw::PointCloudSnapshot frame(std::uint64_t index, float offset) {
  fmcw::PointCloudSnapshot result;
  result.last_frame_id = index + 10U;
  result.scan_frame_index = index;
  result.processing_config_revision = 4U;
  result.width = 2U;
  result.height = 1U;
  result.completed_lines = 1U;
  result.complete = true;
  result.points = {
      point(1.0F + offset, 2.0F, 3.0F, -20.0F, 0.5F),
      point(4.0F + offset, 5.0F, 6.0F, -30.0F, -0.5F),
  };
  return result;
}

void testFmcwMultiFrameReplay(const std::filesystem::path& directory) {
  fmcw::WriterOpenOptions options;
  options.session_directory = directory;
  options.file_stem = "recorded";
  options.session.session_id = "point-cloud-replay-test";
  options.session.coordinate_frame = "ros_x_forward_y_left_z_up";
  options.session.config_snapshot_yaml = "schema_version: 1\n";
  options.session.config_snapshot_json = "{}";

  fmcw::BinaryPointCloudFrameWriter writer;
  std::string error;
  expect(writer.open(options, error), "FMCW point-cloud writer opens");
  expect(writer.write(frame(0U, 0.0F), error) && writer.write(frame(1U, 10.0F), error),
         "FMCW point-cloud writer stores two complete frames");
  expect(writer.finalize(fmcw::WriterFinalizeOptions{0U, "test complete", true}, error),
         "FMCW point-cloud writer finalizes");

  fmcw::PointCloudReplayReader reader;
  expect(reader.open(directory / "recorded.pointcloud.bin", error),
         "FMCWPCD1 replay opens");
  expect(reader.info().multiple_frames && reader.info().has_intensity &&
             reader.info().has_velocity,
         "FMCWPCD1 reports multi-frame XYZIV capabilities");
  fmcw::PointCloudSnapshot replayed;
  expect(reader.readNext(replayed, error) == fmcw::PointCloudReadResult::FrameReady &&
             replayed.scan_frame_index == 0U && replayed.points.size() == 2U &&
             replayed.points[0].valid,
         "FMCWPCD1 reads the first complete organized frame");
  expect(reader.readNext(replayed, error) == fmcw::PointCloudReadResult::FrameReady &&
             replayed.scan_frame_index == 1U && replayed.points[0].x == 11.0F,
         "FMCWPCD1 reads the second frame without recomputing XYZIV");
  expect(reader.readNext(replayed, error) == fmcw::PointCloudReadResult::EndOfStream,
         "FMCWPCD1 reports end of stream");
  expect(reader.rewind(error) &&
             reader.readNext(replayed, error) == fmcw::PointCloudReadResult::FrameReady &&
             replayed.scan_frame_index == 0U,
         "FMCWPCD1 replay rewinds to the first frame");
}

void testAsciiXyzPcd(const std::filesystem::path& directory) {
  const auto path = directory / "xyz_ascii.pcd";
  {
    std::ofstream stream(path, std::ios::binary);
    stream << "# .PCD v0.7\n"
              "VERSION 0.7\n"
              "FIELDS x y z\n"
              "SIZE 4 4 4\n"
              "TYPE F F F\n"
              "COUNT 1 1 1\n"
              "WIDTH 2\n"
              "HEIGHT 1\n"
              "POINTS 2\n"
              "DATA ascii\n"
              "1 2 3\n"
              "nan 5 6\n";
  }
  fmcw::PointCloudReplayReader reader;
  std::string error;
  expect(reader.open(path, error), "ASCII XYZ PCD opens");
  expect(reader.info().format == fmcw::PointCloudInputFormat::PcdAscii &&
             !reader.info().has_intensity && !reader.info().has_velocity,
         "ASCII XYZ PCD reports optional fields as unavailable");
  fmcw::PointCloudSnapshot replayed;
  expect(reader.readNext(replayed, error) == fmcw::PointCloudReadResult::FrameReady &&
             replayed.points[0].valid && !replayed.points[1].valid &&
             std::isnan(replayed.points[0].intensity) &&
             std::isnan(replayed.points[0].velocity),
         "ASCII XYZ PCD preserves geometry validity and missing attributes");
}

void testBinaryXyziPcd(const std::filesystem::path& directory) {
  const auto path = directory / "xyzi_binary.pcd";
  {
    std::ofstream stream(path, std::ios::binary);
    stream << "VERSION 0.7\n"
              "FIELDS x y z intensity\n"
              "SIZE 4 4 4 4\n"
              "TYPE F F F F\n"
              "COUNT 1 1 1 1\n"
              "WIDTH 2\n"
              "HEIGHT 1\n"
              "POINTS 2\n"
              "DATA binary\n";
    const std::array<float, 8> values{{1.0F, 2.0F, 3.0F, 42.0F,
                                       4.0F, 5.0F, 6.0F, 84.0F}};
    stream.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(sizeof(values)));
  }
  fmcw::PointCloudReplayReader reader;
  std::string error;
  expect(reader.open(path, error), "binary XYZI PCD opens");
  expect(reader.info().format == fmcw::PointCloudInputFormat::PcdBinary &&
             reader.info().has_intensity && !reader.info().has_velocity,
         "binary XYZI PCD reports intensity without velocity");
  fmcw::PointCloudSnapshot replayed;
  expect(reader.readNext(replayed, error) == fmcw::PointCloudReadResult::FrameReady &&
             replayed.points[1].intensity == 84.0F &&
             std::isnan(replayed.points[1].velocity),
         "binary XYZI PCD decodes interleaved float fields");
}

void testCsvXyzivAliases(const std::filesystem::path& directory) {
  const auto path = directory / "viewer_export.csv";
  {
    std::ofstream stream(path);
    stream << "x_forward_m,y_left_m,z_up_m,intensity_db,velocity_mps\n"
              "1.5,-2.5,3.5,-44.0,0.25\n";
  }
  fmcw::PointCloudReplayReader reader;
  std::string error;
  expect(reader.open(path, error), "viewer CSV XYZIV opens");
  fmcw::PointCloudSnapshot replayed;
  expect(reader.info().format == fmcw::PointCloudInputFormat::DelimitedText &&
             reader.info().has_intensity && reader.info().has_velocity &&
             reader.readNext(replayed, error) == fmcw::PointCloudReadResult::FrameReady &&
             replayed.points[0].x == 1.5F && replayed.points[0].velocity == 0.25F,
         "viewer CSV aliases map to XYZIV fields");
}

}  // namespace

int main() {
  const auto directory = testDirectory();
  testFmcwMultiFrameReplay(directory);
  testAsciiXyzPcd(directory);
  testBinaryXyziPcd(directory);
  testCsvXyzivAliases(directory);
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  if (failures == 0) {
    std::cout << "All point-cloud replay tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
