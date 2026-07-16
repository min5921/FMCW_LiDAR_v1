#include "storage/binary_storage.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fmcw {
namespace {

constexpr std::array<char, 8> kRawV1Magic{{'F', 'M', 'C', 'W', 'R', 'A', 'W', '1'}};
constexpr std::array<char, 8> kRawV2Magic{{'F', 'M', 'C', 'W', 'R', 'A', 'W', '2'}};
constexpr std::array<char, 8> kProcessedMagic{{'F', 'M', 'C', 'W', 'P', 'R', 'O', '1'}};
constexpr std::uint32_t kRawRecordMagic = 0x314D5246U;
constexpr std::uint32_t kRawBlockMagic = 0x32424D46U;
constexpr std::uint32_t kProcessedRecordMagic = 0x31435250U;
constexpr std::uint32_t kProcessedFormatVersion = 1U;

std::uint64_t utcNowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

bool littleEndianHost() {
  const std::uint16_t value = 1U;
  return *reinterpret_cast<const std::uint8_t*>(&value) == 1U;
}

template <typename Value>
bool writeScalar(std::ostream& stream, const Value& value) {
  static_assert(std::is_arithmetic<Value>::value || std::is_enum<Value>::value, "Scalar required");
  stream.write(reinterpret_cast<const char*>(&value), sizeof(Value));
  return static_cast<bool>(stream);
}

template <typename Value>
bool readScalar(std::istream& stream, Value& value) {
  static_assert(std::is_arithmetic<Value>::value || std::is_enum<Value>::value, "Scalar required");
  stream.read(reinterpret_cast<char*>(&value), sizeof(Value));
  return static_cast<bool>(stream);
}

template <typename Value>
void appendScalar(std::vector<std::uint8_t>& bytes, const Value& value) {
  static_assert(std::is_arithmetic<Value>::value || std::is_enum<Value>::value, "Scalar required");
  const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
  bytes.insert(bytes.end(), first, first + sizeof(Value));
}

bool writeString(std::ostream& stream, const std::string& value) {
  if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  const auto size = static_cast<std::uint32_t>(value.size());
  return writeScalar(stream, size) &&
      (size == 0U || static_cast<bool>(stream.write(value.data(), static_cast<std::streamsize>(size))));
}

bool writeFloatVector(std::ostream& stream, const std::vector<float>& values) {
  if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  const auto size = static_cast<std::uint32_t>(values.size());
  return writeScalar(stream, size) &&
      (size == 0U || static_cast<bool>(stream.write(reinterpret_cast<const char*>(values.data()),
                                                    static_cast<std::streamsize>(size * sizeof(float)))));
}

std::string jsonEscape(const std::string& value) {
  std::ostringstream escaped;
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"': escaped << "\\\""; break;
      case '\\': escaped << "\\\\"; break;
      case '\b': escaped << "\\b"; break;
      case '\f': escaped << "\\f"; break;
      case '\n': escaped << "\\n"; break;
      case '\r': escaped << "\\r"; break;
      case '\t': escaped << "\\t"; break;
      default:
        if (ch < 0x20U) {
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
                  << std::dec << std::setfill(' ');
        } else {
          escaped << static_cast<char>(ch);
        }
    }
  }
  return escaped.str();
}

bool writeSidecar(const std::filesystem::path& path, const WriterOpenOptions& open_options,
                  const WriterFinalizeOptions& finalize_options, const WriterStatus& status,
                  const std::vector<std::filesystem::path>& data_files, const char* stream_type,
                  std::string& error) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    error = "Unable to create metadata sidecar: " + path.string();
    return false;
  }
  stream << "{\n"
         << "  \"session_id\": \"" << jsonEscape(open_options.session.session_id) << "\",\n"
         << "  \"profile_id\": \"" << jsonEscape(open_options.session.profile_id) << "\",\n"
         << "  \"platform\": \"" << jsonEscape(open_options.session.platform) << "\",\n"
         << "  \"application_version\": \"" << jsonEscape(open_options.session.application_version) << "\",\n"
         << "  \"config_schema_version\": " << open_options.session.config_schema_version << ",\n"
         << "  \"start_timestamp_utc_ns\": " << open_options.session.start_timestamp_utc_ns << ",\n"
         << "  \"end_timestamp_utc_ns\": "
         << (finalize_options.end_timestamp_utc_ns == 0U ? utcNowNs() : finalize_options.end_timestamp_utc_ns)
         << ",\n"
         << "  \"stream_type\": \"" << stream_type << "\",\n"
         << "  \"completed\": " << (finalize_options.completed ? "true" : "false") << ",\n"
         << "  \"stop_reason\": \"" << jsonEscape(finalize_options.stop_reason) << "\",\n"
         << "  \"blocks_written\": " << status.blocks_written << ",\n"
         << "  \"frames_written\": " << status.frames_written << ",\n"
         << "  \"bytes_written\": " << status.bytes_written << ",\n"
         << "  \"data_files\": [";
  for (std::size_t index = 0; index < data_files.size(); ++index) {
    if (index != 0U) {
      stream << ", ";
    }
    stream << "\"" << jsonEscape(data_files[index].filename().string()) << "\"";
  }
  stream << "],\n"
         << "  \"raw_stream\": {\n"
         << "    \"format_version\": " << open_options.raw_stream.format_version << ",\n"
         << "    \"channel\": \"" << toString(open_options.raw_stream.channel) << "\",\n"
         << "    \"sample_rate_hz\": " << std::setprecision(17) << open_options.raw_stream.sample_rate_hz << ",\n"
         << "    \"record_length\": " << open_options.raw_stream.record_length << ",\n"
         << "    \"records_per_buffer\": " << open_options.raw_stream.records_per_buffer << "\n"
         << "  },\n"
         << "  \"config_snapshot\": "
         << (open_options.session.config_snapshot_json.empty() ? "{}" : open_options.session.config_snapshot_json)
         << "\n} \n";
  if (!stream) {
    error = "Unable to write metadata sidecar: " + path.string();
    return false;
  }
  error.clear();
  return true;
}

std::uint32_t rawFlags(const RawFrameMetadata& metadata) {
  std::uint32_t flags = 0U;
  flags |= metadata.trigger.valid ? 1U << 0U : 0U;
  flags |= metadata.scan_position.valid ? 1U << 1U : 0U;
  flags |= metadata.optical_state.laser_enabled ? 1U << 2U : 0U;
  flags |= metadata.optical_state.edfa_used ? 1U << 3U : 0U;
  flags |= metadata.optical_state.edfa_output_enabled ? 1U << 4U : 0U;
  return flags;
}

bool writeRawStreamHeader(std::ostream& stream, const RawStreamDescriptor& descriptor) {
  const auto& magic = descriptor.format_version == kRawFrameFormatVersion ? kRawV1Magic : kRawV2Magic;
  stream.write(magic.data(), static_cast<std::streamsize>(magic.size()));
  const auto channel = static_cast<std::uint32_t>(descriptor.channel);
  const auto sample_format = static_cast<std::uint32_t>(descriptor.sample_format);
  const auto byte_order = static_cast<std::uint32_t>(descriptor.byte_order);
  return static_cast<bool>(stream) && writeScalar(stream, descriptor.format_version) &&
      writeScalar(stream, channel) && writeScalar(stream, sample_format) && writeScalar(stream, byte_order) &&
      writeScalar(stream, descriptor.sample_rate_hz) && writeScalar(stream, descriptor.record_length) &&
      (descriptor.format_version == kRawFrameFormatVersion ||
       writeScalar(stream, descriptor.records_per_buffer));
}

bool readRawStreamHeader(std::istream& stream, RawStreamDescriptor& descriptor) {
  std::array<char, 8> magic{};
  stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  std::uint32_t channel = 0U;
  std::uint32_t sample_format = 0U;
  std::uint32_t byte_order = 0U;
  if (!stream || (magic != kRawV1Magic && magic != kRawV2Magic) ||
      !readScalar(stream, descriptor.format_version) ||
      !readScalar(stream, channel) || !readScalar(stream, sample_format) || !readScalar(stream, byte_order) ||
      !readScalar(stream, descriptor.sample_rate_hz) || !readScalar(stream, descriptor.record_length)) {
    return false;
  }
  if (descriptor.format_version == kRawFrameBatchFormatVersion &&
      !readScalar(stream, descriptor.records_per_buffer)) {
    return false;
  }
  descriptor.channel = static_cast<DigitizerChannel>(channel);
  descriptor.sample_format = static_cast<SampleFormat>(sample_format);
  descriptor.byte_order = static_cast<ByteOrder>(byte_order);
  const bool supported_version = descriptor.format_version == kRawFrameFormatVersion ||
      descriptor.format_version == kRawFrameBatchFormatVersion;
  const bool supported_sample_format = descriptor.sample_format == SampleFormat::SignedInt16 ||
      descriptor.sample_format == SampleFormat::UnsignedOffsetBinary12LeftAligned;
  return supported_version && supported_sample_format &&
      descriptor.byte_order == ByteOrder::LittleEndian && descriptor.record_length > 0U;
}

bool writeRawRecord(std::ostream& stream, const RawFrame& frame) {
  const auto& metadata = frame.metadata;
  const auto frame_kind = static_cast<std::uint32_t>(metadata.frame_kind);
  const auto channel = static_cast<std::uint32_t>(metadata.channel);
  const auto sample_format = static_cast<std::uint32_t>(metadata.sample_format);
  const auto byte_order = static_cast<std::uint32_t>(metadata.byte_order);
  const auto sample_count = static_cast<std::uint32_t>(frame.samples.size());
  return writeScalar(stream, kRawRecordMagic) && writeScalar(stream, metadata.format_version) &&
      writeScalar(stream, frame_kind) && writeScalar(stream, metadata.frame_id) &&
      writeScalar(stream, metadata.host_timestamp_ns) && writeScalar(stream, metadata.config_revision) &&
      writeScalar(stream, metadata.trigger.sequence) && writeScalar(stream, metadata.trigger.timestamp_ns) &&
      writeScalar(stream, metadata.trigger.period_jitter_ns) &&
      writeScalar(stream, metadata.trigger.missed_trigger_count) && writeScalar(stream, rawFlags(metadata)) &&
      writeScalar(stream, metadata.scan_position.x_index) && writeScalar(stream, metadata.scan_position.y_index) &&
      writeScalar(stream, metadata.scan_position.x_angle_deg) && writeScalar(stream, metadata.scan_position.y_angle_deg) &&
      writeScalar(stream, metadata.optical_state.revision) && writeScalar(stream, channel) &&
      writeScalar(stream, sample_format) && writeScalar(stream, byte_order) &&
      writeScalar(stream, metadata.sample_rate_hz) && writeScalar(stream, metadata.record_length) &&
      writeScalar(stream, metadata.pre_trigger_samples) && writeScalar(stream, metadata.post_trigger_samples) &&
      writeScalar(stream, metadata.up_segment.start_sample) &&
      writeScalar(stream, metadata.up_segment.end_sample_exclusive) &&
      writeScalar(stream, metadata.down_segment.start_sample) &&
      writeScalar(stream, metadata.down_segment.end_sample_exclusive) && writeScalar(stream, sample_count) &&
      (sample_count == 0U || static_cast<bool>(stream.write(reinterpret_cast<const char*>(frame.samples.data()),
          static_cast<std::streamsize>(sample_count * sizeof(std::int16_t)))));
}

bool writeRawBlock(std::ostream& stream, const RawFrameBatch& batch,
                   std::vector<std::uint8_t>& header,
                   std::vector<std::int16_t>& payload_workspace,
                   std::uint64_t& bytes_written, std::string& error) {
  if (batch.records.empty() || batch.records.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "Raw DMA block has no records or exceeds the v2 record limit";
    return false;
  }
  const auto record_count = static_cast<std::uint32_t>(batch.records.size());
  const auto record_length = batch.metadata.record_length;
  if (record_length == 0U || batch.metadata.record_count != record_count) {
    error = "Raw DMA block metadata does not match its record payload";
    return false;
  }
  const auto sample_count = static_cast<std::size_t>(record_count) * record_length;
  const std::int16_t* payload = nullptr;
  if (batch.hasContiguousSamples()) {
    payload = batch.contiguous_samples.data();
  } else {
    payload_workspace.resize(sample_count);
    for (std::uint32_t record_index = 0; record_index < record_count; ++record_index) {
      const auto& frame = batch.records[record_index];
      if (frame.samples.size() != record_length) {
        error = "Raw DMA block contains a record with an invalid sample count";
        return false;
      }
      std::copy(frame.samples.begin(), frame.samples.end(),
                payload_workspace.begin() + static_cast<std::size_t>(record_index) * record_length);
    }
    payload = payload_workspace.data();
  }

  const auto& common = batch.records.front().metadata;
  header.clear();
  header.reserve(128U + static_cast<std::size_t>(record_count) * 80U);
  appendScalar(header, kRawBlockMagic);
  appendScalar(header, kRawFrameBatchFormatVersion);
  const auto header_size_offset = header.size();
  appendScalar(header, std::uint32_t{0U});
  appendScalar(header, std::uint32_t{0U});
  const auto payload_bytes = static_cast<std::uint64_t>(sample_count) * sizeof(std::int16_t);
  appendScalar(header, payload_bytes);
  appendScalar(header, batch.metadata.sequence);
  appendScalar(header, batch.metadata.completion_timestamp_ns);
  appendScalar(header, batch.metadata.ownership_ready_timestamp_ns);
  appendScalar(header, record_count);
  appendScalar(header, record_length);
  appendScalar(header, batch.metadata.dropped_buffer_count);
  appendScalar(header, batch.metadata.missed_trigger_count);
  appendScalar(header, static_cast<std::uint32_t>(common.frame_kind));
  appendScalar(header, static_cast<std::uint32_t>(common.channel));
  appendScalar(header, static_cast<std::uint32_t>(common.sample_format));
  appendScalar(header, static_cast<std::uint32_t>(common.byte_order));
  appendScalar(header, common.sample_rate_hz);
  appendScalar(header, common.pre_trigger_samples);
  appendScalar(header, common.post_trigger_samples);
  appendScalar(header, common.up_segment.start_sample);
  appendScalar(header, common.up_segment.end_sample_exclusive);
  appendScalar(header, common.down_segment.start_sample);
  appendScalar(header, common.down_segment.end_sample_exclusive);

  for (std::uint32_t record_index = 0; record_index < record_count; ++record_index) {
    const auto& metadata = batch.records[record_index].metadata;
    if (batch.records[record_index].samples.size() != record_length ||
        metadata.record_index_in_buffer != record_index ||
        metadata.records_in_buffer != record_count || metadata.record_length != record_length ||
        metadata.channel != common.channel || metadata.sample_format != common.sample_format ||
        metadata.sample_rate_hz != common.sample_rate_hz ||
        metadata.up_segment.start_sample != common.up_segment.start_sample ||
        metadata.up_segment.end_sample_exclusive != common.up_segment.end_sample_exclusive ||
        metadata.down_segment.start_sample != common.down_segment.start_sample ||
        metadata.down_segment.end_sample_exclusive != common.down_segment.end_sample_exclusive) {
      error = "Raw DMA block records do not share one stream contract";
      return false;
    }
    appendScalar(header, metadata.frame_id);
    appendScalar(header, metadata.host_timestamp_ns);
    appendScalar(header, metadata.config_revision);
    appendScalar(header, metadata.trigger.sequence);
    appendScalar(header, metadata.trigger.timestamp_ns);
    appendScalar(header, metadata.trigger.period_jitter_ns);
    appendScalar(header, metadata.trigger.missed_trigger_count);
    appendScalar(header, rawFlags(metadata));
    appendScalar(header, metadata.scan_position.x_index);
    appendScalar(header, metadata.scan_position.y_index);
    appendScalar(header, metadata.scan_position.x_angle_deg);
    appendScalar(header, metadata.scan_position.y_angle_deg);
    appendScalar(header, metadata.optical_state.revision);
  }

  if (header.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "Raw DMA block metadata exceeds the v2 header limit";
    return false;
  }
  const auto header_bytes = static_cast<std::uint32_t>(header.size());
  std::memcpy(header.data() + header_size_offset, &header_bytes, sizeof(header_bytes));
  stream.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
  stream.write(reinterpret_cast<const char*>(payload),
               static_cast<std::streamsize>(payload_bytes));
  if (!stream) {
    error = "Raw DMA block write failed";
    return false;
  }
  bytes_written = static_cast<std::uint64_t>(header.size()) + payload_bytes;
  error.clear();
  return true;
}

bool writePeak(std::ostream& stream, const PeakMeasurement& peak) {
  const auto state = static_cast<std::uint8_t>(peak.state);
  const auto valid = static_cast<std::uint8_t>(peak.valid ? 1U : 0U);
  return writeScalar(stream, peak.discrete_bin) && writeScalar(stream, peak.peak_bin) &&
      writeScalar(stream, peak.magnitude_db) && writeScalar(stream, state) && writeScalar(stream, valid);
}

bool writeProcessedRecord(std::ostream& stream, const ProcessedFrame& frame) {
  const auto scan_valid = static_cast<std::uint8_t>(frame.scan_position.valid ? 1U : 0U);
  const auto measurement_valid = static_cast<std::uint8_t>(frame.measurement_valid ? 1U : 0U);
  const auto point_valid = static_cast<std::uint8_t>(frame.point.valid ? 1U : 0U);
  const auto stop_requested = static_cast<std::uint8_t>(frame.stop_requested ? 1U : 0U);
  return writeScalar(stream, kProcessedRecordMagic) && writeScalar(stream, frame.frame_id) &&
      writeScalar(stream, frame.source_timestamp_ns) && writeScalar(stream, frame.config_revision) &&
      writeScalar(stream, frame.processing_config_revision) && writeScalar(stream, frame.scan_position.x_index) &&
      writeScalar(stream, frame.scan_position.y_index) && writeScalar(stream, frame.scan_position.x_angle_deg) &&
      writeScalar(stream, frame.scan_position.y_angle_deg) && writeScalar(stream, scan_valid) &&
      writePeak(stream, frame.up_peak) && writePeak(stream, frame.down_peak) &&
      writeScalar(stream, frame.distance_m) && writeScalar(stream, frame.velocity_mps) &&
      writeScalar(stream, frame.point.x) && writeScalar(stream, frame.point.y) && writeScalar(stream, frame.point.z) &&
      writeScalar(stream, frame.point.intensity) && writeScalar(stream, frame.point.velocity) &&
      writeScalar(stream, point_valid) && writeScalar(stream, measurement_valid) && writeScalar(stream, stop_requested) &&
      writeScalar(stream, frame.processing_latency_ms) && writeFloatVector(stream, frame.up_fft_magnitude_db) &&
      writeFloatVector(stream, frame.down_fft_magnitude_db) && writeString(stream, frame.processing_note);
}

double throughputMbps(std::uint64_t bytes, std::chrono::steady_clock::time_point started) {
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return seconds > 0.0 ? static_cast<double>(bytes) * 8.0 / seconds / 1.0e6 : 0.0;
}

}  // namespace

struct BinaryRawFrameWriter::Impl {
  bool closePart(std::string& error) {
    if (!stream.is_open()) {
      error.clear();
      return true;
    }
    stream.flush();
    if (!stream) {
      error = "Raw binary flush failed";
      return false;
    }
    stream.close();
    if (options.preallocate_raw_parts) {
      std::error_code resize_error;
      std::filesystem::resize_file(current_path, current_part_bytes, resize_error);
      if (resize_error) {
        error = "Unable to truncate preallocated raw part: " + resize_error.message();
        return false;
      }
    }
    error.clear();
    return true;
  }

  bool openPart(std::string& error) {
    std::ostringstream suffix;
    suffix << options.file_stem << ".raw." << std::setw(4) << std::setfill('0') << part_index << ".bin";
    const auto path = options.session_directory / suffix.str();
    current_path = path;
    if (options.preallocate_raw_parts) {
      std::ofstream create(path, std::ios::binary | std::ios::trunc);
      if (!create) {
        error = "Unable to create raw binary stream: " + path.string();
        return false;
      }
      create.close();
      const auto block_bytes = static_cast<std::uint64_t>(options.raw_stream.record_length) *
          options.raw_stream.records_per_buffer * sizeof(std::int16_t) + 128U +
          static_cast<std::uint64_t>(options.raw_stream.records_per_buffer) * 80U;
      const auto preallocation_bytes = std::max(split_bytes, block_bytes + 64U);
      std::error_code resize_error;
      std::filesystem::resize_file(path, preallocation_bytes, resize_error);
      if (resize_error) {
        error = "Unable to preallocate raw binary stream: " + resize_error.message();
        return false;
      }
      stream.open(path, std::ios::binary | std::ios::in | std::ios::out);
      stream.seekp(0, std::ios::beg);
    } else {
      stream.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    }
    if (!stream || !writeRawStreamHeader(stream, options.raw_stream)) {
      error = "Unable to create raw binary stream: " + path.string();
      return false;
    }
    data_files.push_back(path);
    current_part_bytes = static_cast<std::uint64_t>(stream.tellp());
    total_bytes += current_part_bytes;
    status.detail = "Writing " + path.filename().string();
    error.clear();
    return true;
  }

  mutable std::mutex mutex;
  WriterOpenOptions options;
  std::fstream stream;
  WriterStatus status;
  std::vector<std::filesystem::path> data_files;
  std::filesystem::path current_path;
  std::vector<std::uint8_t> block_header_workspace;
  std::vector<std::int16_t> block_payload_workspace;
  std::uint32_t part_index = 0U;
  std::uint64_t current_part_bytes = 0U;
  std::uint64_t total_bytes = 0U;
  std::uint64_t split_bytes = 0U;
  std::chrono::steady_clock::time_point started;
  bool finalized = false;
};

BinaryRawFrameWriter::BinaryRawFrameWriter() : impl_(std::make_unique<Impl>()) {}
BinaryRawFrameWriter::~BinaryRawFrameWriter() = default;

bool BinaryRawFrameWriter::open(const WriterOpenOptions& options, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!littleEndianHost()) {
    error = "Raw binary writer supports little-endian hosts only";
    return false;
  }
  if (impl_->status.open || options.session_directory.empty() || options.file_stem.empty() ||
      options.raw_stream.record_length == 0U || options.raw_stream.records_per_buffer == 0U ||
      !(options.raw_stream.sample_rate_hz > 0.0) ||
      (options.raw_stream.format_version != kRawFrameFormatVersion &&
       options.raw_stream.format_version != kRawFrameBatchFormatVersion)) {
    error = "Raw writer open options are invalid or the writer is already open";
    return false;
  }
  std::error_code directory_error;
  std::filesystem::create_directories(options.session_directory, directory_error);
  if (directory_error) {
    error = "Unable to create session directory: " + directory_error.message();
    return false;
  }
  const auto free_space = std::filesystem::space(options.session_directory, directory_error);
  if (directory_error) {
    error = "Unable to query raw storage free space: " + directory_error.message();
    return false;
  }
  const auto estimated_block_bytes = static_cast<std::uint64_t>(options.raw_stream.record_length) *
      options.raw_stream.records_per_buffer * sizeof(std::int16_t) + 128U +
      static_cast<std::uint64_t>(options.raw_stream.records_per_buffer) * 80U;
  const auto split_bytes = static_cast<std::uint64_t>(
      std::max(options.split_file_size_gb, 1.0e-6) * 1.0e9);
  const auto preallocation_bytes = options.preallocate_raw_parts
      ? std::max(split_bytes, estimated_block_bytes + 64U)
      : 0U;
  const auto required_bytes = std::max(
      std::max(options.minimum_free_space_bytes, estimated_block_bytes * 2U),
      preallocation_bytes);
  if (free_space.available < required_bytes) {
    error = "Insufficient free space for two complete raw DMA blocks";
    return false;
  }
  impl_->options = options;
  impl_->stream.clear();
  impl_->data_files.clear();
  impl_->part_index = 0U;
  impl_->current_part_bytes = 0U;
  impl_->total_bytes = 0U;
  impl_->started = std::chrono::steady_clock::now();
  impl_->split_bytes = static_cast<std::uint64_t>(std::max(options.split_file_size_gb, 1.0e-6) * 1.0e9);
  impl_->status = {};
  impl_->status.open = true;
  impl_->status.recording = true;
  impl_->finalized = false;
  if (!impl_->openPart(error)) {
    impl_->status = {};
    return false;
  }
  return true;
}

bool BinaryRawFrameWriter::write(const RawFrame& frame, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->status.open || !impl_->status.recording ||
      impl_->options.raw_stream.format_version != kRawFrameFormatVersion ||
      frame.samples.size() != impl_->options.raw_stream.record_length ||
      frame.metadata.channel != impl_->options.raw_stream.channel ||
      frame.metadata.sample_rate_hz != impl_->options.raw_stream.sample_rate_hz) {
    error = "Raw frame does not match the open stream descriptor";
    return false;
  }
  const auto estimated_record_bytes = static_cast<std::uint64_t>(frame.samples.size()) *
      sizeof(std::int16_t) + 256U;
  if (impl_->status.frames_written > 0U &&
      impl_->current_part_bytes + estimated_record_bytes > impl_->split_bytes) {
    if (!impl_->closePart(error)) {
      impl_->status.detail = error;
      return false;
    }
    impl_->stream.clear();
    ++impl_->part_index;
    impl_->current_part_bytes = 0U;
    if (!impl_->openPart(error)) {
      return false;
    }
  }
  const auto before = impl_->stream.tellp();
  if (!writeRawRecord(impl_->stream, frame)) {
    error = "Raw binary write failed";
    impl_->status.detail = error;
    return false;
  }
  const auto after = impl_->stream.tellp();
  const auto written = static_cast<std::uint64_t>(after - before);
  impl_->current_part_bytes += written;
  impl_->total_bytes += written;
  ++impl_->status.frames_written;
  ++impl_->status.blocks_written;
  impl_->status.bytes_written = impl_->total_bytes;
  if (impl_->options.flush_interval_frames > 0U &&
      impl_->status.frames_written % impl_->options.flush_interval_frames == 0U) {
    impl_->stream.flush();
  }
  impl_->status.throughput_mbps = throughputMbps(impl_->total_bytes, impl_->started);
  error.clear();
  return true;
}

bool BinaryRawFrameWriter::writeBatch(const RawFrameBatch& batch, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->status.open || !impl_->status.recording ||
      impl_->options.raw_stream.format_version != kRawFrameBatchFormatVersion ||
      batch.records.empty() || batch.metadata.record_length != impl_->options.raw_stream.record_length ||
      batch.records.front().metadata.channel != impl_->options.raw_stream.channel ||
      batch.records.front().metadata.sample_format != impl_->options.raw_stream.sample_format ||
      batch.records.front().metadata.sample_rate_hz != impl_->options.raw_stream.sample_rate_hz) {
    error = "Raw DMA block does not match the open v2 stream descriptor";
    return false;
  }
  const auto estimated_block_bytes = static_cast<std::uint64_t>(batch.metadata.record_length) *
      batch.records.size() * sizeof(std::int16_t) + 128U + batch.records.size() * 80U;
  if (impl_->status.blocks_written > 0U &&
      impl_->current_part_bytes + estimated_block_bytes > impl_->split_bytes) {
    if (!impl_->closePart(error)) {
      impl_->status.detail = error;
      return false;
    }
    impl_->stream.clear();
    ++impl_->part_index;
    impl_->current_part_bytes = 0U;
    if (!impl_->openPart(error)) {
      return false;
    }
  }

  std::uint64_t written = 0U;
  if (!writeRawBlock(impl_->stream, batch, impl_->block_header_workspace,
                     impl_->block_payload_workspace, written, error)) {
    impl_->status.detail = error;
    return false;
  }
  impl_->current_part_bytes += written;
  impl_->total_bytes += written;
  ++impl_->status.blocks_written;
  impl_->status.frames_written += batch.records.size();
  impl_->status.bytes_written = impl_->total_bytes;
  if (impl_->options.flush_interval_frames > 0U &&
      impl_->status.frames_written / impl_->options.flush_interval_frames !=
          (impl_->status.frames_written - batch.records.size()) /
              impl_->options.flush_interval_frames) {
    impl_->stream.flush();
  }
  impl_->status.throughput_mbps = throughputMbps(impl_->total_bytes, impl_->started);
  error.clear();
  return true;
}

bool BinaryRawFrameWriter::flush(std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->status.open) {
    error = "Raw writer is not open";
    return false;
  }
  impl_->stream.flush();
  if (!impl_->stream) {
    error = "Raw writer flush failed";
    return false;
  }
  error.clear();
  return true;
}

bool BinaryRawFrameWriter::finalize(const WriterFinalizeOptions& options, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->finalized) {
    error.clear();
    return true;
  }
  WriterFinalizeOptions effective_options = options;
  std::string stream_error;
  if (impl_->stream.is_open()) {
    if (!impl_->closePart(stream_error)) {
      effective_options.completed = false;
      if (effective_options.stop_reason.empty()) {
        effective_options.stop_reason = stream_error;
      }
    }
  }
  impl_->status.recording = false;
  impl_->status.open = false;
  impl_->status.detail = effective_options.completed ? "Raw stream finalized" : "Raw stream stopped with error";
  const auto sidecar = impl_->options.session_directory / (impl_->options.file_stem + ".raw.json");
  if (!writeSidecar(sidecar, impl_->options, effective_options, impl_->status, impl_->data_files, "raw", error)) {
    return false;
  }
  impl_->finalized = true;
  if (!stream_error.empty()) {
    error = stream_error;
    return false;
  }
  return true;
}

WriterStatus BinaryRawFrameWriter::status() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->status;
}

struct BinaryProcessedFrameWriter::Impl {
  mutable std::mutex mutex;
  WriterOpenOptions options;
  std::ofstream stream;
  WriterStatus status;
  std::filesystem::path data_file;
  std::uint64_t bytes_written = 0U;
  std::chrono::steady_clock::time_point started;
  bool finalized = false;
};

BinaryProcessedFrameWriter::BinaryProcessedFrameWriter() : impl_(std::make_unique<Impl>()) {}
BinaryProcessedFrameWriter::~BinaryProcessedFrameWriter() = default;

bool BinaryProcessedFrameWriter::open(const WriterOpenOptions& options, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!littleEndianHost() || impl_->status.open || options.session_directory.empty() || options.file_stem.empty()) {
    error = "Processed writer open options are invalid or the writer is already open";
    return false;
  }
  std::error_code directory_error;
  std::filesystem::create_directories(options.session_directory, directory_error);
  if (directory_error) {
    error = "Unable to create session directory: " + directory_error.message();
    return false;
  }
  impl_->options = options;
  impl_->stream.clear();
  impl_->bytes_written = 0U;
  impl_->data_file = options.session_directory / (options.file_stem + ".processed.bin");
  impl_->stream.open(impl_->data_file, std::ios::binary | std::ios::trunc);
  impl_->stream.write(kProcessedMagic.data(), static_cast<std::streamsize>(kProcessedMagic.size()));
  if (!impl_->stream || !writeScalar(impl_->stream, kProcessedFormatVersion)) {
    error = "Unable to create processed binary stream";
    return false;
  }
  impl_->bytes_written = static_cast<std::uint64_t>(impl_->stream.tellp());
  impl_->started = std::chrono::steady_clock::now();
  impl_->status = {};
  impl_->status.open = true;
  impl_->status.recording = true;
  impl_->status.detail = "Writing " + impl_->data_file.filename().string();
  impl_->finalized = false;
  error.clear();
  return true;
}

bool BinaryProcessedFrameWriter::write(const ProcessedFrame& frame, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->status.open || !impl_->status.recording) {
    error = "Processed writer is not recording";
    return false;
  }
  const auto before = impl_->stream.tellp();
  if (!writeProcessedRecord(impl_->stream, frame)) {
    error = "Processed binary write failed";
    impl_->status.detail = error;
    return false;
  }
  const auto after = impl_->stream.tellp();
  impl_->bytes_written += static_cast<std::uint64_t>(after - before);
  ++impl_->status.frames_written;
  impl_->status.bytes_written = impl_->bytes_written;
  if (impl_->options.flush_interval_frames > 0U &&
      impl_->status.frames_written % impl_->options.flush_interval_frames == 0U) {
    impl_->stream.flush();
  }
  impl_->status.throughput_mbps = throughputMbps(impl_->bytes_written, impl_->started);
  error.clear();
  return true;
}

bool BinaryProcessedFrameWriter::flush(std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->status.open) {
    error = "Processed writer is not open";
    return false;
  }
  impl_->stream.flush();
  if (!impl_->stream) {
    error = "Processed writer flush failed";
    return false;
  }
  error.clear();
  return true;
}

bool BinaryProcessedFrameWriter::finalize(const WriterFinalizeOptions& options, std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->finalized) {
    error.clear();
    return true;
  }
  WriterFinalizeOptions effective_options = options;
  std::string stream_error;
  if (impl_->stream.is_open()) {
    impl_->stream.flush();
    if (!impl_->stream) {
      stream_error = "Processed binary final flush failed";
      effective_options.completed = false;
      if (effective_options.stop_reason.empty()) {
        effective_options.stop_reason = stream_error;
      }
    }
    impl_->stream.close();
  }
  impl_->status.recording = false;
  impl_->status.open = false;
  impl_->status.detail = effective_options.completed ? "Processed stream finalized" : "Processed stream stopped with error";
  const auto sidecar = impl_->options.session_directory / (impl_->options.file_stem + ".processed.json");
  if (!writeSidecar(sidecar, impl_->options, effective_options, impl_->status, {impl_->data_file}, "processed", error)) {
    return false;
  }
  impl_->finalized = true;
  if (!stream_error.empty()) {
    error = stream_error;
    return false;
  }
  return true;
}

WriterStatus BinaryProcessedFrameWriter::status() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->status;
}

struct RawReplayReader::Impl {
  bool openNextPart(std::string& error) {
    if (!numbered_parts) {
      error.clear();
      return false;
    }
    std::ostringstream next_name;
    next_name << part_prefix << std::setw(4) << std::setfill('0') << (part_index + 1U) << ".bin";
    const std::filesystem::path next_path(next_name.str());
    if (!std::filesystem::exists(next_path)) {
      error.clear();
      return false;
    }
    stream.close();
    stream.clear();
    stream.open(next_path, std::ios::binary);
    RawStreamDescriptor next_descriptor;
    if (!stream || !readRawStreamHeader(stream, next_descriptor) ||
        next_descriptor.format_version != descriptor.format_version ||
        next_descriptor.channel != descriptor.channel ||
        next_descriptor.sample_format != descriptor.sample_format ||
        next_descriptor.byte_order != descriptor.byte_order ||
        next_descriptor.sample_rate_hz != descriptor.sample_rate_hz ||
        next_descriptor.record_length != descriptor.record_length ||
        next_descriptor.records_per_buffer != descriptor.records_per_buffer) {
      error = "Next raw replay part has an incompatible stream header";
      return false;
    }
    ++part_index;
    error.clear();
    return true;
  }

  std::ifstream stream;
  RawStreamDescriptor descriptor;
  std::string part_prefix;
  std::uint32_t part_index = 0U;
  bool numbered_parts = false;
  RawFrameBatch cached_batch;
  std::size_t cached_record_index = 0U;
};

RawReplayReader::RawReplayReader() : impl_(std::make_unique<Impl>()) {}
RawReplayReader::~RawReplayReader() = default;

bool RawReplayReader::open(const std::filesystem::path& path, std::string& error) {
  close();
  if (!littleEndianHost()) {
    error = "Raw replay supports little-endian hosts only";
    return false;
  }
  impl_->stream.open(path, std::ios::binary);
  if (!impl_->stream || !readRawStreamHeader(impl_->stream, impl_->descriptor)) {
    close();
    error = "Raw replay file header is invalid: " + path.string();
    return false;
  }
  const auto path_text = path.string();
  const auto marker = path_text.rfind(".raw.");
  if (marker != std::string::npos && marker + 9U < path_text.size() &&
      path_text.substr(marker + 9U) == ".bin") {
    const auto digits = path_text.substr(marker + 5U, 4U);
    if (digits.size() == 4U && std::all_of(digits.begin(), digits.end(), [](unsigned char ch) {
          return ch >= '0' && ch <= '9';
        })) {
      impl_->part_prefix = path_text.substr(0, marker + 5U);
      impl_->part_index = static_cast<std::uint32_t>(std::stoul(digits));
      impl_->numbered_parts = true;
    }
  }
  error.clear();
  return true;
}

ReplayReadResult RawReplayReader::readNext(RawFrame& frame, std::string& error) {
  if (!impl_->stream.is_open()) {
    error = "Raw replay reader is not open";
    return ReplayReadResult::Error;
  }
  if (impl_->descriptor.format_version == kRawFrameBatchFormatVersion) {
    if (impl_->cached_record_index >= impl_->cached_batch.records.size()) {
      impl_->cached_batch = {};
      impl_->cached_record_index = 0U;
      const auto result = readNextBatch(impl_->cached_batch, error);
      if (result != ReplayReadResult::FrameReady) {
        return result;
      }
    }
    frame = impl_->cached_batch.records[impl_->cached_record_index++];
    error.clear();
    return ReplayReadResult::FrameReady;
  }
  std::uint32_t record_magic = 0U;
  impl_->stream.read(reinterpret_cast<char*>(&record_magic), sizeof(record_magic));
  if (impl_->stream.eof() && impl_->stream.gcount() == 0) {
    if (impl_->numbered_parts) {
      std::ostringstream next_name;
      next_name << impl_->part_prefix << std::setw(4) << std::setfill('0') << (impl_->part_index + 1U) << ".bin";
      const std::filesystem::path next_path(next_name.str());
      if (std::filesystem::exists(next_path)) {
        impl_->stream.close();
        impl_->stream.clear();
        impl_->stream.open(next_path, std::ios::binary);
        RawStreamDescriptor next_descriptor;
        if (!impl_->stream || !readRawStreamHeader(impl_->stream, next_descriptor) ||
            next_descriptor.channel != impl_->descriptor.channel ||
            next_descriptor.sample_format != impl_->descriptor.sample_format ||
            next_descriptor.byte_order != impl_->descriptor.byte_order ||
            next_descriptor.sample_rate_hz != impl_->descriptor.sample_rate_hz ||
            next_descriptor.record_length != impl_->descriptor.record_length) {
          error = "Next raw replay part has an incompatible stream header";
          return ReplayReadResult::Error;
        }
        ++impl_->part_index;
        return readNext(frame, error);
      }
    }
    error.clear();
    return ReplayReadResult::EndOfStream;
  }
  frame = {};
  std::uint32_t frame_kind = 0U;
  std::uint32_t flags = 0U;
  std::uint32_t channel = 0U;
  std::uint32_t sample_format = 0U;
  std::uint32_t byte_order = 0U;
  std::uint32_t sample_count = 0U;
  auto& metadata = frame.metadata;
  if (!impl_->stream || record_magic != kRawRecordMagic || !readScalar(impl_->stream, metadata.format_version) ||
      !readScalar(impl_->stream, frame_kind) || !readScalar(impl_->stream, metadata.frame_id) ||
      !readScalar(impl_->stream, metadata.host_timestamp_ns) || !readScalar(impl_->stream, metadata.config_revision) ||
      !readScalar(impl_->stream, metadata.trigger.sequence) || !readScalar(impl_->stream, metadata.trigger.timestamp_ns) ||
      !readScalar(impl_->stream, metadata.trigger.period_jitter_ns) ||
      !readScalar(impl_->stream, metadata.trigger.missed_trigger_count) || !readScalar(impl_->stream, flags) ||
      !readScalar(impl_->stream, metadata.scan_position.x_index) ||
      !readScalar(impl_->stream, metadata.scan_position.y_index) ||
      !readScalar(impl_->stream, metadata.scan_position.x_angle_deg) ||
      !readScalar(impl_->stream, metadata.scan_position.y_angle_deg) ||
      !readScalar(impl_->stream, metadata.optical_state.revision) || !readScalar(impl_->stream, channel) ||
      !readScalar(impl_->stream, sample_format) || !readScalar(impl_->stream, byte_order) ||
      !readScalar(impl_->stream, metadata.sample_rate_hz) || !readScalar(impl_->stream, metadata.record_length) ||
      !readScalar(impl_->stream, metadata.pre_trigger_samples) ||
      !readScalar(impl_->stream, metadata.post_trigger_samples) ||
      !readScalar(impl_->stream, metadata.up_segment.start_sample) ||
      !readScalar(impl_->stream, metadata.up_segment.end_sample_exclusive) ||
      !readScalar(impl_->stream, metadata.down_segment.start_sample) ||
      !readScalar(impl_->stream, metadata.down_segment.end_sample_exclusive) ||
      !readScalar(impl_->stream, sample_count) || sample_count != metadata.record_length ||
      (sample_format != static_cast<std::uint32_t>(SampleFormat::SignedInt16) &&
       sample_format != static_cast<std::uint32_t>(SampleFormat::UnsignedOffsetBinary12LeftAligned)) ||
      static_cast<SampleFormat>(sample_format) != impl_->descriptor.sample_format ||
      byte_order != static_cast<std::uint32_t>(ByteOrder::LittleEndian) ||
      sample_count > 100000000U) {
    error = "Raw replay record header is corrupt";
    return ReplayReadResult::Error;
  }
  metadata.frame_kind = static_cast<FrameKind>(frame_kind);
  metadata.channel = static_cast<DigitizerChannel>(channel);
  metadata.sample_format = static_cast<SampleFormat>(sample_format);
  metadata.byte_order = static_cast<ByteOrder>(byte_order);
  metadata.trigger.valid = (flags & (1U << 0U)) != 0U;
  metadata.scan_position.valid = (flags & (1U << 1U)) != 0U;
  metadata.optical_state.laser_enabled = (flags & (1U << 2U)) != 0U;
  metadata.optical_state.edfa_used = (flags & (1U << 3U)) != 0U;
  metadata.optical_state.edfa_output_enabled = (flags & (1U << 4U)) != 0U;
  frame.samples.resize(sample_count);
  impl_->stream.read(reinterpret_cast<char*>(frame.samples.data()),
                     static_cast<std::streamsize>(sample_count * sizeof(std::int16_t)));
  if (!impl_->stream) {
    error = "Raw replay sample payload is truncated";
    return ReplayReadResult::Error;
  }
  error.clear();
  return ReplayReadResult::FrameReady;
}

ReplayReadResult RawReplayReader::readNextBatch(RawFrameBatch& batch, std::string& error) {
  batch.metadata = {};
  batch.contiguous_samples.clear();
  batch.records.clear();
  if (!impl_->stream.is_open()) {
    error = "Raw replay reader is not open";
    return ReplayReadResult::Error;
  }
  if (impl_->descriptor.format_version != kRawFrameBatchFormatVersion) {
    error = "Raw v1 replay does not contain DMA block records";
    return ReplayReadResult::Error;
  }

  std::uint32_t block_magic = 0U;
  while (true) {
    impl_->stream.read(reinterpret_cast<char*>(&block_magic), sizeof(block_magic));
    if (impl_->stream.eof() && impl_->stream.gcount() == 0) {
      std::string next_error;
      if (impl_->openNextPart(next_error)) {
        continue;
      }
      if (!next_error.empty()) {
        error = std::move(next_error);
        return ReplayReadResult::Error;
      }
      error.clear();
      return ReplayReadResult::EndOfStream;
    }
    break;
  }

  const auto block_start = impl_->stream.tellg() - static_cast<std::streamoff>(sizeof(block_magic));
  std::uint32_t block_version = 0U;
  std::uint32_t header_bytes = 0U;
  std::uint32_t reserved = 0U;
  std::uint64_t payload_bytes = 0U;
  std::uint32_t record_count = 0U;
  std::uint32_t record_length = 0U;
  std::uint32_t frame_kind = 0U;
  std::uint32_t channel = 0U;
  std::uint32_t sample_format = 0U;
  std::uint32_t byte_order = 0U;
  double sample_rate_hz = 0.0;
  std::uint32_t pre_trigger_samples = 0U;
  std::uint32_t post_trigger_samples = 0U;
  SegmentRange up_segment;
  SegmentRange down_segment;
  auto& block = batch.metadata;
  if (!impl_->stream || block_magic != kRawBlockMagic ||
      !readScalar(impl_->stream, block_version) ||
      !readScalar(impl_->stream, header_bytes) || !readScalar(impl_->stream, reserved) ||
      !readScalar(impl_->stream, payload_bytes) || !readScalar(impl_->stream, block.sequence) ||
      !readScalar(impl_->stream, block.completion_timestamp_ns) ||
      !readScalar(impl_->stream, block.ownership_ready_timestamp_ns) ||
      !readScalar(impl_->stream, record_count) || !readScalar(impl_->stream, record_length) ||
      !readScalar(impl_->stream, block.dropped_buffer_count) ||
      !readScalar(impl_->stream, block.missed_trigger_count) ||
      !readScalar(impl_->stream, frame_kind) || !readScalar(impl_->stream, channel) ||
      !readScalar(impl_->stream, sample_format) || !readScalar(impl_->stream, byte_order) ||
      !readScalar(impl_->stream, sample_rate_hz) ||
      !readScalar(impl_->stream, pre_trigger_samples) ||
      !readScalar(impl_->stream, post_trigger_samples) ||
      !readScalar(impl_->stream, up_segment.start_sample) ||
      !readScalar(impl_->stream, up_segment.end_sample_exclusive) ||
      !readScalar(impl_->stream, down_segment.start_sample) ||
      !readScalar(impl_->stream, down_segment.end_sample_exclusive) ||
      block_version != kRawFrameBatchFormatVersion || record_count == 0U ||
      record_count > 1000000U || record_length == 0U || record_length > 100000000U ||
      (sample_format != static_cast<std::uint32_t>(SampleFormat::SignedInt16) &&
       sample_format != static_cast<std::uint32_t>(SampleFormat::UnsignedOffsetBinary12LeftAligned)) ||
      static_cast<SampleFormat>(sample_format) != impl_->descriptor.sample_format ||
      byte_order != static_cast<std::uint32_t>(ByteOrder::LittleEndian)) {
    error = "Raw v2 DMA block header is corrupt";
    return ReplayReadResult::Error;
  }
  const auto expected_samples = static_cast<std::uint64_t>(record_count) * record_length;
  if (expected_samples > std::numeric_limits<std::size_t>::max() ||
      payload_bytes != expected_samples * sizeof(std::int16_t)) {
    error = "Raw v2 DMA payload size is invalid";
    return ReplayReadResult::Error;
  }

  block.format_version = block_version;
  block.record_count = record_count;
  block.record_length = record_length;
  batch.contiguous_samples.resize(static_cast<std::size_t>(expected_samples));
  batch.records.resize(record_count);
  for (std::uint32_t record_index = 0; record_index < record_count; ++record_index) {
    auto& frame = batch.records[record_index];
    auto& metadata = frame.metadata;
    std::uint32_t flags = 0U;
    if (!readScalar(impl_->stream, metadata.frame_id) ||
        !readScalar(impl_->stream, metadata.host_timestamp_ns) ||
        !readScalar(impl_->stream, metadata.config_revision) ||
        !readScalar(impl_->stream, metadata.trigger.sequence) ||
        !readScalar(impl_->stream, metadata.trigger.timestamp_ns) ||
        !readScalar(impl_->stream, metadata.trigger.period_jitter_ns) ||
        !readScalar(impl_->stream, metadata.trigger.missed_trigger_count) ||
        !readScalar(impl_->stream, flags) ||
        !readScalar(impl_->stream, metadata.scan_position.x_index) ||
        !readScalar(impl_->stream, metadata.scan_position.y_index) ||
        !readScalar(impl_->stream, metadata.scan_position.x_angle_deg) ||
        !readScalar(impl_->stream, metadata.scan_position.y_angle_deg) ||
        !readScalar(impl_->stream, metadata.optical_state.revision)) {
      error = "Raw v2 record metadata table is truncated";
      return ReplayReadResult::Error;
    }
    metadata.format_version = kRawFrameFormatVersion;
    metadata.frame_kind = static_cast<FrameKind>(frame_kind);
    metadata.dma_buffer_sequence = block.sequence;
    metadata.record_index_in_buffer = record_index;
    metadata.records_in_buffer = record_count;
    metadata.channel = static_cast<DigitizerChannel>(channel);
    metadata.sample_format = static_cast<SampleFormat>(sample_format);
    metadata.byte_order = static_cast<ByteOrder>(byte_order);
    metadata.sample_rate_hz = sample_rate_hz;
    metadata.record_length = record_length;
    metadata.pre_trigger_samples = pre_trigger_samples;
    metadata.post_trigger_samples = post_trigger_samples;
    metadata.up_segment = up_segment;
    metadata.down_segment = down_segment;
    metadata.trigger.valid = (flags & (1U << 0U)) != 0U;
    metadata.scan_position.valid = (flags & (1U << 1U)) != 0U;
    metadata.optical_state.laser_enabled = (flags & (1U << 2U)) != 0U;
    metadata.optical_state.edfa_used = (flags & (1U << 3U)) != 0U;
    metadata.optical_state.edfa_output_enabled = (flags & (1U << 4U)) != 0U;
    frame.samples.setView(batch.contiguous_samples.data() +
        static_cast<std::size_t>(record_index) * record_length, record_length);
  }

  const auto consumed_header_bytes = static_cast<std::uint64_t>(impl_->stream.tellg() - block_start);
  if (consumed_header_bytes != header_bytes) {
    error = "Raw v2 DMA block header length is inconsistent";
    return ReplayReadResult::Error;
  }
  impl_->stream.read(reinterpret_cast<char*>(batch.contiguous_samples.data()),
                     static_cast<std::streamsize>(payload_bytes));
  if (!impl_->stream) {
    error = "Raw v2 DMA payload is truncated";
    return ReplayReadResult::Error;
  }
  error.clear();
  return ReplayReadResult::FrameReady;
}

void RawReplayReader::close() {
  if (impl_->stream.is_open()) {
    impl_->stream.close();
  }
  impl_->stream.clear();
  impl_->descriptor = {};
  impl_->part_prefix.clear();
  impl_->part_index = 0U;
  impl_->numbered_parts = false;
  impl_->cached_batch = {};
  impl_->cached_record_index = 0U;
}

bool RawReplayReader::isOpen() const { return impl_->stream.is_open(); }

const RawStreamDescriptor& RawReplayReader::streamDescriptor() const { return impl_->descriptor; }

}  // namespace fmcw
