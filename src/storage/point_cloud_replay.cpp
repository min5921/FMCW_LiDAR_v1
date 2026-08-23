#include "storage/point_cloud_replay.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fmcw {
namespace {

constexpr std::array<char, 8> kPointCloudMagic{{'F', 'M', 'C', 'W', 'P', 'C', 'D', '1'}};
constexpr std::uint32_t kFmcwPointCloudFormatVersion = 1U;
constexpr std::uint32_t kWaymoPointCloudFormatVersion = 2U;
constexpr std::uint32_t kPointCloudFrameMagic = 0x31444350U;
constexpr std::uint64_t kMaximumPointCount = 20U * 1000U * 1000U;
constexpr std::uint64_t kMaximumPointBytes = 512U * 1024U * 1024U;

bool littleEndianHost() {
  const std::uint16_t value = 1U;
  return *reinterpret_cast<const std::uint8_t*>(&value) == 1U;
}

template <typename Value>
bool readScalar(std::istream& stream, Value& value) {
  stream.read(reinterpret_cast<char*>(&value), sizeof(Value));
  return static_cast<bool>(stream);
}

std::string trim(std::string value) {
  const auto not_space = [](unsigned char character) { return !std::isspace(character); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string upper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

std::vector<std::string> splitWhitespace(const std::string& line) {
  std::istringstream stream(line);
  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token) {
    tokens.push_back(std::move(token));
  }
  return tokens;
}

std::vector<std::string> splitDelimited(std::string line) {
  for (auto& character : line) {
    if (character == ',' || character == ';' || character == '\t') {
      character = ' ';
    }
  }
  auto tokens = splitWhitespace(line);
  for (auto& token : tokens) {
    if (token.size() >= 2U &&
        ((token.front() == '"' && token.back() == '"') ||
         (token.front() == '\'' && token.back() == '\''))) {
      token = token.substr(1U, token.size() - 2U);
    }
  }
  return tokens;
}

bool parseDouble(const std::string& token, double& value) {
  errno = 0;
  char* end = nullptr;
  value = std::strtod(token.c_str(), &end);
  return end != token.c_str() && *end == '\0' && errno != ERANGE;
}

bool parseUnsigned(const std::string& token, std::uint64_t& value) {
  try {
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(token, &consumed, 10);
    if (consumed != token.size()) {
      return false;
    }
    value = parsed;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool checkedPointCount(std::uint64_t width, std::uint64_t height,
                       std::uint64_t& count, std::string& error) {
  if (width == 0U || height == 0U || width > std::numeric_limits<std::uint32_t>::max() ||
      height > std::numeric_limits<std::uint32_t>::max() ||
      width > kMaximumPointCount / height) {
    error = "Point cloud dimensions are invalid or exceed the replay limit";
    return false;
  }
  count = width * height;
  if (count > kMaximumPointCount) {
    error = "Point cloud exceeds the 20,000,000 point replay limit";
    return false;
  }
  return true;
}

void finishSnapshot(PointCloudSnapshot& frame) {
  frame.completed_lines = frame.height;
  frame.complete = true;
  for (auto& point : frame.points) {
    point.valid = point.valid && std::isfinite(point.x) &&
        std::isfinite(point.y) && std::isfinite(point.z);
  }
}

struct PcdField {
  std::string name;
  std::uint32_t size = 0U;
  char type = '\0';
  std::uint32_t count = 1U;
  std::size_t component_offset = 0U;
  std::size_t byte_offset = 0U;
};

struct PcdHeader {
  std::vector<PcdField> fields;
  std::uint32_t width = 0U;
  std::uint32_t height = 1U;
  std::uint64_t points = 0U;
  std::size_t components_per_point = 0U;
  std::size_t bytes_per_point = 0U;
  std::string data;
};

std::optional<std::size_t> findField(const PcdHeader& header,
                                     std::initializer_list<std::string_view> names) {
  for (std::size_t index = 0U; index < header.fields.size(); ++index) {
    for (const auto name : names) {
      if (header.fields[index].name == name) {
        return index;
      }
    }
  }
  return std::nullopt;
}

bool parsePcdHeader(std::istream& stream, PcdHeader& header, std::string& error) {
  std::vector<std::string> names;
  std::vector<std::uint32_t> sizes;
  std::vector<char> types;
  std::vector<std::uint32_t> counts;
  bool found_data = false;
  std::string line;
  while (std::getline(stream, line)) {
    if (line.size() > 1024U * 1024U) {
      error = "PCD header line exceeds 1 MiB";
      return false;
    }
    line = trim(line);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto tokens = splitWhitespace(line);
    if (tokens.empty()) {
      continue;
    }
    const auto key = upper(tokens.front());
    if (key == "FIELDS" || key == "FIELD") {
      names.assign(tokens.begin() + 1, tokens.end());
      for (auto& name : names) {
        name = lower(name);
      }
    } else if (key == "SIZE") {
      sizes.clear();
      for (std::size_t index = 1U; index < tokens.size(); ++index) {
        std::uint64_t value = 0U;
        if (!parseUnsigned(tokens[index], value) || value == 0U || value > 8U) {
          error = "PCD SIZE contains an unsupported value";
          return false;
        }
        sizes.push_back(static_cast<std::uint32_t>(value));
      }
    } else if (key == "TYPE") {
      types.clear();
      for (std::size_t index = 1U; index < tokens.size(); ++index) {
        if (tokens[index].size() != 1U) {
          error = "PCD TYPE entry is invalid";
          return false;
        }
        types.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(tokens[index].front()))));
      }
    } else if (key == "COUNT") {
      counts.clear();
      for (std::size_t index = 1U; index < tokens.size(); ++index) {
        std::uint64_t value = 0U;
        if (!parseUnsigned(tokens[index], value) || value == 0U ||
            value > std::numeric_limits<std::uint32_t>::max()) {
          error = "PCD COUNT contains an invalid value";
          return false;
        }
        counts.push_back(static_cast<std::uint32_t>(value));
      }
    } else if (key == "WIDTH" || key == "HEIGHT" || key == "POINTS") {
      if (tokens.size() != 2U) {
        error = "PCD dimension entry is invalid";
        return false;
      }
      std::uint64_t value = 0U;
      if (!parseUnsigned(tokens[1], value) || value > std::numeric_limits<std::uint32_t>::max()) {
        error = "PCD dimension exceeds the supported range";
        return false;
      }
      if (key == "WIDTH") {
        header.width = static_cast<std::uint32_t>(value);
      } else if (key == "HEIGHT") {
        header.height = static_cast<std::uint32_t>(value);
      } else {
        header.points = value;
      }
    } else if (key == "DATA") {
      if (tokens.size() != 2U) {
        error = "PCD DATA entry is invalid";
        return false;
      }
      header.data = lower(tokens[1]);
      found_data = true;
      break;
    }
  }

  if (!found_data || names.empty() || sizes.size() != names.size() ||
      types.size() != names.size()) {
    error = "PCD header is incomplete; FIELDS, SIZE, TYPE, and DATA are required";
    return false;
  }
  if (counts.empty()) {
    counts.assign(names.size(), 1U);
  }
  if (counts.size() != names.size()) {
    error = "PCD COUNT does not match FIELDS";
    return false;
  }
  if (header.data != "ascii" && header.data != "binary") {
    error = header.data == "binary_compressed"
        ? "PCD DATA binary_compressed is not supported; use ASCII or binary PCD"
        : "PCD DATA must be ascii or binary";
    return false;
  }

  std::size_t component_offset = 0U;
  std::size_t byte_offset = 0U;
  for (std::size_t index = 0U; index < names.size(); ++index) {
    const auto type = types[index];
    const auto size = sizes[index];
    const bool supported = (type == 'F' && (size == 4U || size == 8U)) ||
        ((type == 'I' || type == 'U') &&
         (size == 1U || size == 2U || size == 4U || size == 8U));
    if (!supported) {
      error = "PCD field '" + names[index] + "' has an unsupported TYPE/SIZE";
      return false;
    }
    if (counts[index] > (std::numeric_limits<std::size_t>::max() - byte_offset) / size) {
      error = "PCD point layout overflows the supported range";
      return false;
    }
    header.fields.push_back(PcdField{
        names[index], size, type, counts[index], component_offset, byte_offset});
    component_offset += counts[index];
    byte_offset += static_cast<std::size_t>(size) * counts[index];
  }
  header.components_per_point = component_offset;
  header.bytes_per_point = byte_offset;

  if (!findField(header, {"x"}) || !findField(header, {"y"}) ||
      !findField(header, {"z"})) {
    error = "PCD input requires x, y, and z fields";
    return false;
  }
  if (header.points == 0U) {
    std::uint64_t calculated = 0U;
    if (!checkedPointCount(header.width, header.height, calculated, error)) {
      return false;
    }
    header.points = calculated;
  }
  if (header.width == 0U) {
    header.width = static_cast<std::uint32_t>(header.points);
    header.height = 1U;
  }
  std::uint64_t calculated = 0U;
  if (!checkedPointCount(header.width, header.height, calculated, error) ||
      calculated != header.points) {
    error = "PCD WIDTH x HEIGHT does not match POINTS";
    return false;
  }
  if (header.bytes_per_point == 0U ||
      header.points > kMaximumPointBytes / header.bytes_per_point) {
    error = "PCD payload exceeds the 512 MiB replay limit";
    return false;
  }
  return true;
}

template <typename Value>
double copiedValue(const std::uint8_t* data) {
  Value value{};
  std::memcpy(&value, data, sizeof(Value));
  return static_cast<double>(value);
}

bool decodeBinaryValue(const std::uint8_t* data, const PcdField& field, double& value) {
  if (field.type == 'F') {
    if (field.size == 4U) value = copiedValue<float>(data);
    else if (field.size == 8U) value = copiedValue<double>(data);
    else return false;
  } else if (field.type == 'I') {
    if (field.size == 1U) value = copiedValue<std::int8_t>(data);
    else if (field.size == 2U) value = copiedValue<std::int16_t>(data);
    else if (field.size == 4U) value = copiedValue<std::int32_t>(data);
    else if (field.size == 8U) value = copiedValue<std::int64_t>(data);
    else return false;
  } else if (field.type == 'U') {
    if (field.size == 1U) value = copiedValue<std::uint8_t>(data);
    else if (field.size == 2U) value = copiedValue<std::uint16_t>(data);
    else if (field.size == 4U) value = copiedValue<std::uint32_t>(data);
    else if (field.size == 8U) value = copiedValue<std::uint64_t>(data);
    else return false;
  } else {
    return false;
  }
  return true;
}

PointXYZI pointFromValues(double x, double y, double z,
                          std::optional<double> intensity,
                          std::optional<double> velocity,
                          std::optional<double> elongation,
                          std::optional<double> valid,
                          std::optional<double> scan_x = std::nullopt,
                          std::optional<double> scan_y = std::nullopt) {
  PointXYZI point;
  point.x = static_cast<float>(x);
  point.y = static_cast<float>(y);
  point.z = static_cast<float>(z);
  if (intensity.has_value()) point.intensity = static_cast<float>(*intensity);
  if (velocity.has_value()) point.velocity = static_cast<float>(*velocity);
  if (elongation.has_value()) point.elongation = static_cast<float>(*elongation);
  if (scan_x.has_value()) point.scan_x_command = static_cast<float>(*scan_x);
  if (scan_y.has_value()) point.scan_y_command = static_cast<float>(*scan_y);
  point.valid = (!valid.has_value() || *valid != 0.0) &&
      std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
  return point;
}

bool loadPcd(const std::filesystem::path& path, PointCloudSnapshot& frame,
             PointCloudReplayInfo& info, std::string& error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = "Unable to open PCD file";
    return false;
  }
  PcdHeader header;
  if (!parsePcdHeader(stream, header, error)) {
    return false;
  }
  if (header.data == "binary" && !littleEndianHost()) {
    error = "Binary PCD replay requires a little-endian host";
    return false;
  }

  const auto x_index = *findField(header, {"x"});
  const auto y_index = *findField(header, {"y"});
  const auto z_index = *findField(header, {"z"});
  const auto intensity_index = findField(header, {"intensity", "intensity_db", "i"});
  const auto velocity_index = findField(header, {"velocity", "velocity_mps", "v"});
  const auto elongation_index = findField(header, {"elongation"});
  const auto valid_index = findField(header, {"valid"});

  frame = {};
  frame.last_frame_id = 1U;
  frame.scan_frame_index = 0U;
  frame.width = header.width;
  frame.height = header.height;
  try {
    frame.points.resize(static_cast<std::size_t>(header.points));
  } catch (const std::exception&) {
    error = "PCD point allocation failed";
    return false;
  }

  if (header.data == "ascii") {
    std::vector<double> values(header.components_per_point);
    std::string token;
    for (std::uint64_t point_index = 0U; point_index < header.points; ++point_index) {
      for (std::size_t component = 0U; component < values.size(); ++component) {
        if (!(stream >> token) || !parseDouble(token, values[component])) {
          error = "PCD ASCII payload ended early or contains a non-numeric value";
          return false;
        }
      }
      const auto get = [&](std::size_t index) {
        return values[header.fields[index].component_offset];
      };
      frame.points[static_cast<std::size_t>(point_index)] = pointFromValues(
          get(x_index), get(y_index), get(z_index),
          intensity_index ? std::optional<double>(get(*intensity_index)) : std::nullopt,
          velocity_index ? std::optional<double>(get(*velocity_index)) : std::nullopt,
          elongation_index ? std::optional<double>(get(*elongation_index)) : std::nullopt,
          valid_index ? std::optional<double>(get(*valid_index)) : std::nullopt);
    }
  } else {
    std::vector<std::uint8_t> point_bytes(header.bytes_per_point);
    for (std::uint64_t point_index = 0U; point_index < header.points; ++point_index) {
      stream.read(reinterpret_cast<char*>(point_bytes.data()),
                  static_cast<std::streamsize>(point_bytes.size()));
      if (!stream) {
        error = "PCD binary payload ended early";
        return false;
      }
      const auto get = [&](std::size_t index, double& value) {
        return decodeBinaryValue(point_bytes.data() + header.fields[index].byte_offset,
                                 header.fields[index], value);
      };
      double x = 0.0;
      double y = 0.0;
      double z = 0.0;
      double intensity = 0.0;
      double velocity = 0.0;
      double elongation = 0.0;
      double valid = 1.0;
      if (!get(x_index, x) || !get(y_index, y) || !get(z_index, z) ||
          (intensity_index && !get(*intensity_index, intensity)) ||
          (velocity_index && !get(*velocity_index, velocity)) ||
          (elongation_index && !get(*elongation_index, elongation)) ||
          (valid_index && !get(*valid_index, valid))) {
        error = "PCD binary payload uses an unsupported field encoding";
        return false;
      }
      frame.points[static_cast<std::size_t>(point_index)] = pointFromValues(
          x, y, z,
          intensity_index ? std::optional<double>(intensity) : std::nullopt,
          velocity_index ? std::optional<double>(velocity) : std::nullopt,
          elongation_index ? std::optional<double>(elongation) : std::nullopt,
          valid_index ? std::optional<double>(valid) : std::nullopt);
    }
  }

  finishSnapshot(frame);
  info.format = header.data == "ascii" ? PointCloudInputFormat::PcdAscii
                                        : PointCloudInputFormat::PcdBinary;
  info.width = header.width;
  info.height = header.height;
  info.point_count = header.points;
  info.organized = header.height > 1U;
  info.has_intensity = intensity_index.has_value();
  info.has_velocity = velocity_index.has_value();
  info.has_elongation = elongation_index.has_value();
  if (info.has_elongation) {
    frame.feature_encoding = PointCloudFeatureEncoding::CenterPointWaymo;
  }
  info.multiple_frames = false;
  info.format_name = header.data == "ascii" ? "PCD ASCII" : "PCD binary";
  error.clear();
  return true;
}

std::optional<std::size_t> findDelimitedField(
    const std::vector<std::string>& names,
    std::initializer_list<std::string_view> aliases) {
  for (std::size_t index = 0U; index < names.size(); ++index) {
    for (const auto alias : aliases) {
      if (names[index] == alias) {
        return index;
      }
    }
  }
  return std::nullopt;
}

bool loadDelimited(const std::filesystem::path& path, PointCloudSnapshot& frame,
                   PointCloudReplayInfo& info, std::string& error) {
  std::ifstream stream(path);
  if (!stream) {
    error = "Unable to open point-cloud text file";
    return false;
  }

  std::vector<std::string> first_tokens;
  std::string line;
  std::size_t line_number = 0U;
  while (std::getline(stream, line)) {
    ++line_number;
    line = trim(line);
    if (!line.empty() && line.front() != '#') {
      first_tokens = splitDelimited(line);
      break;
    }
  }
  if (first_tokens.size() < 3U) {
    error = "Point-cloud text input requires at least three columns";
    return false;
  }

  bool first_is_numeric = true;
  for (const auto& token : first_tokens) {
    double value = 0.0;
    if (!parseDouble(token, value)) {
      first_is_numeric = false;
      break;
    }
  }

  std::optional<std::size_t> x_index;
  std::optional<std::size_t> y_index;
  std::optional<std::size_t> z_index;
  std::optional<std::size_t> intensity_index;
  std::optional<std::size_t> velocity_index;
  std::optional<std::size_t> elongation_index;
  std::optional<std::size_t> valid_index;
  std::optional<std::size_t> scan_x_index;
  std::optional<std::size_t> scan_y_index;
  if (first_is_numeric) {
    x_index = 0U;
    y_index = 1U;
    z_index = 2U;
    if (first_tokens.size() >= 4U) intensity_index = 3U;
    if (first_tokens.size() >= 5U) velocity_index = 4U;
  } else {
    std::vector<std::string> names;
    names.reserve(first_tokens.size());
    for (auto token : first_tokens) {
      names.push_back(lower(trim(std::move(token))));
    }
    x_index = findDelimitedField(names, {"x", "x_forward_m"});
    y_index = findDelimitedField(names, {"y", "y_left_m"});
    z_index = findDelimitedField(names, {"z", "z_up_m"});
    intensity_index = findDelimitedField(names, {"intensity", "intensity_db", "i"});
    velocity_index = findDelimitedField(names, {"velocity", "velocity_mps", "v"});
    elongation_index = findDelimitedField(names, {"elongation"});
    valid_index = findDelimitedField(names, {"valid"});
    scan_x_index = findDelimitedField(names, {"scan_x_command", "scan_x"});
    scan_y_index = findDelimitedField(names, {"scan_y_command", "scan_y"});
    if (!x_index || !y_index || !z_index) {
      error = "Point-cloud text header requires x/y/z or x_forward_m/y_left_m/z_up_m";
      return false;
    }
  }

  frame = {};
  frame.last_frame_id = 1U;
  frame.scan_frame_index = 0U;
  const auto parse_row = [&](const std::vector<std::string>& tokens,
                             std::size_t row_number, PointXYZI& point,
                             std::string& row_error) {
    std::size_t required_index = std::max({*x_index, *y_index, *z_index});
    for (const auto index : {intensity_index, velocity_index, elongation_index, valid_index,
                             scan_x_index, scan_y_index}) {
      if (index) required_index = std::max(required_index, *index);
    }
    if (tokens.size() <= required_index) {
      row_error = "Point-cloud text row " + std::to_string(row_number) + " has too few columns";
      return false;
    }
    const auto value = [&](std::size_t index, double& output) {
      return parseDouble(tokens[index], output);
    };
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double intensity = 0.0;
    double velocity = 0.0;
    double elongation = 0.0;
    double valid = 1.0;
    double scan_x = 0.0;
    double scan_y = 0.0;
    if (!value(*x_index, x) || !value(*y_index, y) || !value(*z_index, z) ||
        (intensity_index && !value(*intensity_index, intensity)) ||
        (velocity_index && !value(*velocity_index, velocity)) ||
        (elongation_index && !value(*elongation_index, elongation)) ||
        (valid_index && !value(*valid_index, valid)) ||
        (scan_x_index && !value(*scan_x_index, scan_x)) ||
        (scan_y_index && !value(*scan_y_index, scan_y))) {
      row_error = "Point-cloud text row " + std::to_string(row_number) +
          " contains a non-numeric value";
      return false;
    }
    point = pointFromValues(
        x, y, z,
        intensity_index ? std::optional<double>(intensity) : std::nullopt,
        velocity_index ? std::optional<double>(velocity) : std::nullopt,
        elongation_index ? std::optional<double>(elongation) : std::nullopt,
        valid_index ? std::optional<double>(valid) : std::nullopt,
        scan_x_index ? std::optional<double>(scan_x) : std::nullopt,
        scan_y_index ? std::optional<double>(scan_y) : std::nullopt);
    return true;
  };

  if (first_is_numeric) {
    PointXYZI point;
    if (!parse_row(first_tokens, line_number, point, error)) {
      return false;
    }
    frame.points.push_back(point);
  }
  while (std::getline(stream, line)) {
    ++line_number;
    line = trim(line);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    PointXYZI point;
    if (!parse_row(splitDelimited(line), line_number, point, error)) {
      return false;
    }
    frame.points.push_back(point);
    if (frame.points.size() > kMaximumPointCount) {
      error = "Point-cloud text input exceeds the 20,000,000 point replay limit";
      return false;
    }
  }
  if (frame.points.empty() || frame.points.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "Point-cloud text input does not contain any points";
    return false;
  }
  frame.width = static_cast<std::uint32_t>(frame.points.size());
  frame.height = 1U;
  finishSnapshot(frame);

  info.format = PointCloudInputFormat::DelimitedText;
  info.width = frame.width;
  info.height = frame.height;
  info.point_count = frame.points.size();
  info.organized = false;
  info.has_intensity = intensity_index.has_value();
  info.has_velocity = velocity_index.has_value();
  info.has_elongation = elongation_index.has_value();
  if (info.has_elongation) {
    frame.feature_encoding = PointCloudFeatureEncoding::CenterPointWaymo;
  }
  info.multiple_frames = false;
  info.format_name = "XYZ text";
  if (info.has_velocity) info.format_name = "XYZIV text";
  else if (info.has_intensity) info.format_name = "XYZI text";
  error.clear();
  return true;
}

}  // namespace

struct PointCloudReplayReader::Impl {
  PointCloudReplayInfo info;
  std::filesystem::path path;
  std::ifstream stream;
  std::streamoff data_offset = 0;
  std::uint64_t file_size = 0U;
  std::uint32_t format_version = 0U;
  PointCloudSnapshot single_frame;
  bool single_consumed = false;
  bool open = false;
};

PointCloudReplayReader::PointCloudReplayReader() : impl_(std::make_unique<Impl>()) {}
PointCloudReplayReader::~PointCloudReplayReader() = default;

bool PointCloudReplayReader::open(const std::filesystem::path& path, std::string& error) {
  close();
  std::error_code filesystem_error;
  if (!std::filesystem::is_regular_file(path, filesystem_error) || filesystem_error) {
    error = "Point-cloud replay path is not a readable file";
    return false;
  }

  std::ifstream probe(path, std::ios::binary);
  std::array<char, 8> magic{};
  probe.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  const bool fmcw_binary = probe.gcount() == static_cast<std::streamsize>(magic.size()) &&
      magic == kPointCloudMagic;
  probe.close();

  if (fmcw_binary) {
    if (!littleEndianHost()) {
      error = "FMCW point-cloud replay requires a little-endian host";
      return false;
    }
    impl_->stream.open(path, std::ios::binary);
    std::uint32_t version = 0U;
    impl_->stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!impl_->stream || !readScalar(impl_->stream, version) ||
        (version != kFmcwPointCloudFormatVersion &&
         version != kWaymoPointCloudFormatVersion)) {
      close();
      error = "FMCW point-cloud header is invalid or unsupported";
      return false;
    }
    impl_->path = path;
    impl_->data_offset = impl_->stream.tellg();
    impl_->file_size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) {
      close();
      error = "Unable to determine point-cloud file size";
      return false;
    }
    impl_->format_version = version;
    impl_->info.format = version == kWaymoPointCloudFormatVersion
        ? PointCloudInputFormat::WaymoBinaryV2
        : PointCloudInputFormat::FmcwBinaryV1;
    impl_->info.has_intensity = true;
    impl_->info.has_velocity = version == kFmcwPointCloudFormatVersion;
    impl_->info.has_elongation = version == kWaymoPointCloudFormatVersion;
    impl_->info.multiple_frames = true;
    impl_->info.format_name = version == kWaymoPointCloudFormatVersion
        ? "WaymoPCD2"
        : "FMCWPCD1";
    impl_->open = true;
    error.clear();
    return true;
  }

  PointCloudReplayInfo info;
  PointCloudSnapshot frame;
  const auto extension = lower(path.extension().string());
  const bool loaded = extension == ".pcd"
      ? loadPcd(path, frame, info, error)
      : loadDelimited(path, frame, info, error);
  if (!loaded) {
    close();
    return false;
  }
  impl_->path = path;
  impl_->info = std::move(info);
  impl_->single_frame = std::move(frame);
  impl_->single_consumed = false;
  impl_->open = true;
  error.clear();
  return true;
}

PointCloudReadResult PointCloudReplayReader::readNext(PointCloudSnapshot& frame,
                                                      std::string& error) {
  if (!impl_->open) {
    error = "Point-cloud replay reader is not open";
    return PointCloudReadResult::Error;
  }
  const bool binary_stream = impl_->info.format == PointCloudInputFormat::FmcwBinaryV1 ||
      impl_->info.format == PointCloudInputFormat::WaymoBinaryV2;
  if (!binary_stream) {
    if (impl_->single_consumed) {
      error.clear();
      return PointCloudReadResult::EndOfStream;
    }
    frame = impl_->single_frame;
    impl_->single_consumed = true;
    error.clear();
    return PointCloudReadResult::FrameReady;
  }

  if (impl_->stream.peek() == std::char_traits<char>::eof()) {
    error.clear();
    return PointCloudReadResult::EndOfStream;
  }
  std::uint32_t frame_magic = 0U;
  std::uint64_t last_frame_id = 0U;
  std::uint64_t scan_frame_index = 0U;
  std::uint64_t processing_revision = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t point_count = 0U;
  if (!readScalar(impl_->stream, frame_magic) ||
      !readScalar(impl_->stream, last_frame_id) ||
      !readScalar(impl_->stream, scan_frame_index) ||
      !readScalar(impl_->stream, processing_revision) ||
      !readScalar(impl_->stream, width) || !readScalar(impl_->stream, height) ||
      !readScalar(impl_->stream, point_count)) {
    error = "FMCW point-cloud frame header is truncated";
    return PointCloudReadResult::Error;
  }
  std::uint64_t expected_count = 0U;
  if (frame_magic != kPointCloudFrameMagic ||
      !checkedPointCount(width, height, expected_count, error) ||
      expected_count != point_count) {
    if (error.empty()) error = "FMCW point-cloud frame dimensions are invalid";
    return PointCloudReadResult::Error;
  }
  constexpr std::uint64_t point_bytes = sizeof(float) * 5U + sizeof(std::uint8_t);
  const auto payload_bytes = expected_count * point_bytes;
  const auto position = impl_->stream.tellg();
  if (position < 0 || static_cast<std::uint64_t>(position) > impl_->file_size ||
      payload_bytes > impl_->file_size - static_cast<std::uint64_t>(position)) {
    error = "FMCW point-cloud frame payload is truncated";
    return PointCloudReadResult::Error;
  }

  frame = {};
  frame.last_frame_id = last_frame_id;
  frame.scan_frame_index = scan_frame_index;
  frame.processing_config_revision = processing_revision;
  frame.width = width;
  frame.height = height;
  try {
    frame.points.resize(point_count);
  } catch (const std::exception&) {
    error = "FMCW point-cloud frame allocation failed";
    return PointCloudReadResult::Error;
  }
  for (auto& point : frame.points) {
    std::uint8_t valid = 0U;
    float fifth = std::numeric_limits<float>::quiet_NaN();
    if (!readScalar(impl_->stream, point.x) || !readScalar(impl_->stream, point.y) ||
        !readScalar(impl_->stream, point.z) || !readScalar(impl_->stream, point.intensity) ||
        !readScalar(impl_->stream, fifth) || !readScalar(impl_->stream, valid)) {
      error = "FMCW point-cloud point payload is truncated";
      return PointCloudReadResult::Error;
    }
    if (impl_->format_version == kWaymoPointCloudFormatVersion) {
      point.elongation = fifth;
    } else {
      point.velocity = fifth;
    }
    point.valid = valid != 0U;
  }
  frame.feature_encoding = impl_->format_version == kWaymoPointCloudFormatVersion
      ? PointCloudFeatureEncoding::CenterPointWaymo
      : PointCloudFeatureEncoding::FmcwDbVelocity;
  finishSnapshot(frame);
  if (impl_->info.point_count == 0U) {
    impl_->info.width = width;
    impl_->info.height = height;
    impl_->info.point_count = point_count;
    impl_->info.organized = height > 1U;
  }
  error.clear();
  return PointCloudReadResult::FrameReady;
}

bool PointCloudReplayReader::rewind(std::string& error) {
  if (!impl_->open) {
    error = "Point-cloud replay reader is not open";
    return false;
  }
  if (impl_->info.format == PointCloudInputFormat::FmcwBinaryV1 ||
      impl_->info.format == PointCloudInputFormat::WaymoBinaryV2) {
    impl_->stream.clear();
    impl_->stream.seekg(impl_->data_offset);
    if (!impl_->stream) {
      error = "Unable to rewind FMCW point-cloud stream";
      return false;
    }
  } else {
    impl_->single_consumed = false;
  }
  error.clear();
  return true;
}

void PointCloudReplayReader::close() {
  if (impl_->stream.is_open()) {
    impl_->stream.close();
  }
  impl_->info = {};
  impl_->path.clear();
  impl_->data_offset = 0;
  impl_->file_size = 0U;
  impl_->format_version = 0U;
  impl_->single_frame = {};
  impl_->single_consumed = false;
  impl_->open = false;
}

bool PointCloudReplayReader::isOpen() const { return impl_->open; }

const PointCloudReplayInfo& PointCloudReplayReader::info() const { return impl_->info; }

}  // namespace fmcw
