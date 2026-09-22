#include "detection/point_cloud_file_loader.h"
#include "storage/point_cloud_replay.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fmcw {
namespace {

constexpr std::size_t kMaximumImportPoints = 10'000'000U;

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

bool littleEndianHost() {
  const std::uint16_t value = 1U;
  return *reinterpret_cast<const std::uint8_t*>(&value) == 1U;
}

std::shared_ptr<PointCloudSnapshot> makeSnapshot(std::uint64_t scan_frame_index,
                                                 std::size_t point_count) {
  auto snapshot = std::make_shared<PointCloudSnapshot>();
  snapshot->last_frame_id = scan_frame_index;
  snapshot->scan_frame_index = scan_frame_index;
  snapshot->processing_config_revision = 1U;
  snapshot->width = static_cast<std::uint32_t>(std::min<std::size_t>(
      point_count, std::numeric_limits<std::uint32_t>::max()));
  snapshot->height = 1U;
  snapshot->completed_lines = 1U;
  snapshot->complete = true;
  snapshot->feature_encoding = PointCloudFeatureEncoding::CenterPointWaymo;
  snapshot->points.reserve(point_count);
  return snapshot;
}

void appendPoint(PointCloudSnapshot& snapshot, float x, float y, float z,
                 float intensity, float elongation) {
  PointXYZI point;
  point.x = x;
  point.y = y;
  point.z = z;
  point.intensity = intensity;
  point.elongation = elongation;
  point.velocity = 0.0F;
  point.valid = std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
      std::isfinite(intensity) && std::isfinite(elongation);
  snapshot.points.push_back(point);
}

bool loadBinary(const std::filesystem::path& path, std::uint64_t scan_frame_index,
                PointCloudFileLoadResult& result, std::string& error) {
  std::error_code size_error;
  const auto byte_count = std::filesystem::file_size(path, size_error);
  if (size_error || byte_count == 0U) {
    error = size_error ? size_error.message() : "point binary is empty";
    return false;
  }

  const bool divisible_by_five = byte_count % (5U * sizeof(float)) == 0U;
  const bool divisible_by_six = byte_count % (6U * sizeof(float)) == 0U;
  const auto file_name = lower(path.filename().string());
  const bool named_waymo_return = file_name.find("_return1.bin") != std::string::npos ||
      file_name.find("_return2.bin") != std::string::npos;
  int feature_count = 0;
  if (named_waymo_return && divisible_by_six) {
    feature_count = 6;
  } else if (file_name == "points.bin" && divisible_by_five) {
    feature_count = 5;
  } else if (divisible_by_five != divisible_by_six) {
    feature_count = divisible_by_five ? 5 : 6;
  } else if (divisible_by_five && divisible_by_six) {
    error = "ambiguous .bin layout; name CenterPoint Nx5 input points.bin or use a Waymo *_returnN.bin name";
    return false;
  } else {
    error = "point binary size is not divisible by Nx5 or Nx6 float32 records";
    return false;
  }

  const auto point_count = static_cast<std::size_t>(
      byte_count / (static_cast<std::uintmax_t>(feature_count) * sizeof(float)));
  if (point_count == 0U || point_count > kMaximumImportPoints) {
    error = "point binary exceeds the supported 1..10,000,000 point range";
    return false;
  }
  std::vector<float> values(point_count * static_cast<std::size_t>(feature_count));
  std::ifstream stream(path, std::ios::binary);
  stream.read(reinterpret_cast<char*>(values.data()),
              static_cast<std::streamsize>(values.size() * sizeof(float)));
  if (!stream) {
    error = "point binary payload is truncated";
    return false;
  }

  auto snapshot = makeSnapshot(scan_frame_index, point_count);
  for (std::size_t index = 0; index < point_count; ++index) {
    const auto offset = index * static_cast<std::size_t>(feature_count);
    const auto intensity = feature_count == 6
        ? std::tanh(values[offset + 3U])
        : values[offset + 3U];
    appendPoint(*snapshot, values[offset], values[offset + 1U], values[offset + 2U],
                intensity, values[offset + 4U]);
  }
  result.snapshot = std::move(snapshot);
  result.format_detail = feature_count == 6
      ? "Waymo Nx6 float32 (tanh intensity, nlz column ignored)"
      : "CenterPoint Nx5 float32 (preprocessed intensity, elongation)";
  error.clear();
  return true;
}

bool loadPcd(const std::filesystem::path& path, std::uint64_t scan_frame_index,
             PointCloudFileLoadResult& result, std::string& error) {
  PointCloudReplayReader reader;
  auto snapshot = std::make_shared<PointCloudSnapshot>();
  if (!reader.open(path, error) ||
      reader.readNext(*snapshot, error) != PointCloudReadResult::FrameReady) { return false; }
  if (snapshot->points.size() > kMaximumImportPoints) {
    error = "Detection import exceeds the 10,000,000 point limit";
    return false;
  }
  const auto info = reader.info();
  snapshot->last_frame_id = scan_frame_index;
  snapshot->scan_frame_index = scan_frame_index;
  snapshot->processing_config_revision = 1U;
  // Detection imports already carry preprocessed features, unlike raw FMCW dB data.
  snapshot->feature_encoding = PointCloudFeatureEncoding::CenterPointWaymo;
  for (auto& point : snapshot->points) {
    if (!info.has_intensity) point.intensity = 0.0F;
    if (!info.has_elongation) point.elongation = 0.0F;
    point.velocity = 0.0F;
    point.valid = point.valid && std::isfinite(point.intensity) && std::isfinite(point.elongation);
  }
  result.snapshot = std::move(snapshot);
  result.format_detail = info.format_name + " (preprocessed intensity)";
  error.clear();
  return true;
}

}  // namespace

bool loadCenterPointCloudFile(const std::filesystem::path& path,
                              std::uint64_t scan_frame_index,
                              PointCloudFileLoadResult& result,
                              std::string& error) {
  result = {};
  if (!littleEndianHost()) {
    error = "point cloud import requires a little-endian host";
    return false;
  }
  const auto extension = lower(path.extension().string());
  if (extension == ".bin") {
    return loadBinary(path, scan_frame_index, result, error);
  }
  if (extension == ".pcd") {
    return loadPcd(path, scan_frame_index, result, error);
  }
  error = "supported point cloud formats are .bin and .pcd";
  return false;
}

}  // namespace fmcw
