#include "detection/point_cloud_file_loader.h"

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

std::vector<std::string> tokens(const std::string& line) {
  std::istringstream stream(line);
  std::vector<std::string> values;
  for (std::string value; stream >> value;) {
    values.push_back(std::move(value));
  }
  return values;
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

struct PcdField {
  std::string name;
  std::size_t size = 0U;
  char type = 0;
  std::size_t count = 1U;
  std::size_t byte_offset = 0U;
  std::size_t scalar_offset = 0U;
};

double binaryScalar(const char* data, const PcdField& field) {
  const auto* source = data + field.byte_offset;
  if (field.type == 'F' && field.size == 4U) {
    float value = 0.0F;
    std::memcpy(&value, source, sizeof(value));
    return value;
  }
  if (field.type == 'F' && field.size == 8U) {
    double value = 0.0;
    std::memcpy(&value, source, sizeof(value));
    return value;
  }
  if (field.type == 'I') {
    if (field.size == 1U) { std::int8_t value; std::memcpy(&value, source, 1U); return value; }
    if (field.size == 2U) { std::int16_t value; std::memcpy(&value, source, 2U); return value; }
    if (field.size == 4U) { std::int32_t value; std::memcpy(&value, source, 4U); return value; }
    if (field.size == 8U) { std::int64_t value; std::memcpy(&value, source, 8U); return static_cast<double>(value); }
  }
  if (field.type == 'U') {
    if (field.size == 1U) { std::uint8_t value; std::memcpy(&value, source, 1U); return value; }
    if (field.size == 2U) { std::uint16_t value; std::memcpy(&value, source, 2U); return value; }
    if (field.size == 4U) { std::uint32_t value; std::memcpy(&value, source, 4U); return value; }
    if (field.size == 8U) { std::uint64_t value; std::memcpy(&value, source, 8U); return static_cast<double>(value); }
  }
  throw std::runtime_error("unsupported PCD field type or size");
}

bool loadPcd(const std::filesystem::path& path, std::uint64_t scan_frame_index,
             PointCloudFileLoadResult& result, std::string& error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = "PCD file could not be opened";
    return false;
  }

  std::vector<std::string> names;
  std::vector<std::size_t> sizes;
  std::vector<char> types;
  std::vector<std::size_t> counts;
  std::size_t width = 0U;
  std::size_t height = 1U;
  std::size_t point_count = 0U;
  std::string data_mode;
  for (std::string line; std::getline(stream, line);) {
    const auto values = tokens(line);
    if (values.empty() || values[0][0] == '#') {
      continue;
    }
    const auto key = lower(values[0]);
    try {
      if ((key == "fields" || key == "field") && values.size() > 1U) {
        names.assign(values.begin() + 1, values.end());
      } else if (key == "size") {
        for (std::size_t index = 1; index < values.size(); ++index) sizes.push_back(std::stoul(values[index]));
      } else if (key == "type") {
        for (std::size_t index = 1; index < values.size(); ++index) types.push_back(static_cast<char>(std::toupper(values[index][0])));
      } else if (key == "count") {
        for (std::size_t index = 1; index < values.size(); ++index) counts.push_back(std::stoul(values[index]));
      } else if (key == "width" && values.size() == 2U) {
        width = std::stoull(values[1]);
      } else if (key == "height" && values.size() == 2U) {
        height = std::stoull(values[1]);
      } else if (key == "points" && values.size() == 2U) {
        point_count = std::stoull(values[1]);
      } else if (key == "data" && values.size() == 2U) {
        data_mode = lower(values[1]);
        break;
      }
    } catch (const std::exception&) {
      error = "PCD header contains an invalid numeric value";
      return false;
    }
  }

  if (point_count == 0U && width != 0U && height != 0U &&
      width <= kMaximumImportPoints / height) {
    point_count = width * height;
  }
  if (names.empty() || sizes.size() != names.size() || types.size() != names.size() ||
      point_count == 0U || point_count > kMaximumImportPoints || data_mode.empty()) {
    error = "PCD header is incomplete or exceeds the supported point limit";
    return false;
  }
  if (counts.empty()) counts.assign(names.size(), 1U);
  if (counts.size() != names.size()) {
    error = "PCD COUNT does not match FIELDS";
    return false;
  }

  std::vector<PcdField> fields;
  fields.reserve(names.size());
  std::size_t point_step = 0U;
  std::size_t scalar_step = 0U;
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (sizes[index] == 0U || counts[index] == 0U ||
        sizes[index] > 8U || counts[index] > 1024U) {
      error = "PCD field size/count is unsupported";
      return false;
    }
    fields.push_back({lower(names[index]), sizes[index], types[index], counts[index],
                      point_step, scalar_step});
    point_step += sizes[index] * counts[index];
    scalar_step += counts[index];
  }
  const auto findField = [&fields](std::string_view name) -> const PcdField* {
    const auto found = std::find_if(fields.begin(), fields.end(), [name](const PcdField& field) {
      return field.name == name;
    });
    return found == fields.end() ? nullptr : &*found;
  };
  const auto* x_field = findField("x");
  const auto* y_field = findField("y");
  const auto* z_field = findField("z");
  const auto* intensity_field = findField("intensity");
  const auto* elongation_field = findField("elongation");
  if (x_field == nullptr || y_field == nullptr || z_field == nullptr) {
    error = "PCD requires x, y, and z fields";
    return false;
  }

  auto snapshot = makeSnapshot(scan_frame_index, point_count);
  snapshot->width = static_cast<std::uint32_t>(std::min<std::size_t>(
      width == 0U ? point_count : width, std::numeric_limits<std::uint32_t>::max()));
  snapshot->height = static_cast<std::uint32_t>(std::min<std::size_t>(
      height, std::numeric_limits<std::uint32_t>::max()));
  snapshot->completed_lines = snapshot->height;
  try {
    if (data_mode == "ascii") {
      for (std::size_t point_index = 0; point_index < point_count;) {
        std::string line;
        if (!std::getline(stream, line)) {
          error = "PCD ASCII payload is truncated";
          return false;
        }
        const auto values = tokens(line);
        if (values.empty()) continue;
        if (values.size() < scalar_step) {
          error = "PCD ASCII point has too few fields";
          return false;
        }
        const auto scalar = [&values](const PcdField* field, double fallback) {
          return field == nullptr ? fallback : std::stod(values[field->scalar_offset]);
        };
        appendPoint(*snapshot, static_cast<float>(scalar(x_field, 0.0)),
                    static_cast<float>(scalar(y_field, 0.0)),
                    static_cast<float>(scalar(z_field, 0.0)),
                    static_cast<float>(scalar(intensity_field, 0.0)),
                    static_cast<float>(scalar(elongation_field, 0.0)));
        ++point_index;
      }
    } else if (data_mode == "binary") {
      if (!littleEndianHost()) {
        error = "binary PCD import requires a little-endian host";
        return false;
      }
      if (point_step == 0U || point_count >
          std::numeric_limits<std::size_t>::max() / point_step) {
        error = "PCD binary payload size overflows the host container";
        return false;
      }
      std::vector<char> payload(point_count * point_step);
      stream.read(payload.data(), static_cast<std::streamsize>(payload.size()));
      if (!stream) {
        error = "PCD binary payload is truncated";
        return false;
      }
      for (std::size_t point_index = 0; point_index < point_count; ++point_index) {
        const auto* point = payload.data() + point_index * point_step;
        const auto scalar = [point](const PcdField* field, double fallback) {
          return field == nullptr ? fallback : binaryScalar(point, *field);
        };
        appendPoint(*snapshot, static_cast<float>(scalar(x_field, 0.0)),
                    static_cast<float>(scalar(y_field, 0.0)),
                    static_cast<float>(scalar(z_field, 0.0)),
                    static_cast<float>(scalar(intensity_field, 0.0)),
                    static_cast<float>(scalar(elongation_field, 0.0)));
      }
    } else {
      error = "PCD DATA must be ascii or binary; binary_compressed is not supported";
      return false;
    }
  } catch (const std::exception& exception) {
    error = std::string("PCD payload is invalid: ") + exception.what();
    return false;
  }

  result.snapshot = std::move(snapshot);
  result.format_detail = "PCD " + data_mode +
      " (preprocessed intensity, elongation=" +
      (elongation_field == nullptr ? std::string("0") : std::string("field")) + ")";
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
