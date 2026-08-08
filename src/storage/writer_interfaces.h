#pragma once

#include "core/config_types.h"
#include "core/frame_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace fmcw {

struct WriterStatus {
  bool open = false;
  bool recording = false;
  std::uint64_t blocks_written = 0;
  std::uint64_t frames_written = 0;
  std::uint64_t dropped_frames = 0;
  std::uint64_t bytes_written = 0;
  double throughput_mbps = 0.0;
  std::string detail;
};

struct SessionDescriptor {
  std::string session_id;
  std::string profile_id;
  std::string platform;
  std::string application_version;
  std::uint32_t config_schema_version = kConfigSchemaVersion;
  std::uint64_t start_timestamp_utc_ns = 0;
  std::string coordinate_frame = "ros_x_forward_y_left_z_up";
  std::string config_snapshot_json;
  std::string config_snapshot_yaml;
};

struct RawStreamDescriptor {
  std::uint32_t format_version = kRawFrameBatchFormatVersion;
  DigitizerChannel channel = DigitizerChannel::A;
  SampleFormat sample_format = SampleFormat::SignedInt16;
  ByteOrder byte_order = ByteOrder::LittleEndian;
  double sample_rate_hz = 0.0;
  std::uint32_t record_length = 0;
  std::uint32_t records_per_buffer = 1;
};

struct WriterOpenOptions {
  std::filesystem::path session_directory;
  std::string file_stem;
  SessionDescriptor session;
  RawStreamDescriptor raw_stream;
  bool raw_enabled = true;
  bool processed_enabled = false;
  std::size_t queue_capacity = 64;
  QueueOverflowPolicy overflow_policy = QueueOverflowPolicy::StopAcquisition;
  double split_file_size_gb = 4.0;
  std::uint32_t flush_interval_frames = 128;
  bool preallocate_raw_parts = true;
  std::uint64_t minimum_free_space_bytes = 64U * 1024U * 1024U;
};

struct WriterFinalizeOptions {
  std::uint64_t end_timestamp_utc_ns = 0;
  std::string stop_reason;
  bool completed = true;
};

class IRawFrameWriter {
 public:
  virtual ~IRawFrameWriter() = default;
  virtual bool open(const WriterOpenOptions& options, std::string& error) = 0;
  virtual bool write(const RawFrame& frame, std::string& error) = 0;
  virtual bool writeBatch(const RawFrameBatch& batch, std::string& error) {
    for (const auto& frame : batch.records) {
      if (!write(frame, error)) {
        return false;
      }
    }
    error.clear();
    return true;
  }
  virtual bool flush(std::string& error) = 0;
  virtual bool finalize(const WriterFinalizeOptions& options, std::string& error) = 0;
  virtual WriterStatus status() const = 0;
};

class IProcessedFrameWriter {
 public:
  virtual ~IProcessedFrameWriter() = default;
  virtual bool open(const WriterOpenOptions& options, std::string& error) = 0;
  virtual bool write(const ProcessedFrame& frame, std::string& error) = 0;
  virtual bool flush(std::string& error) = 0;
  virtual bool finalize(const WriterFinalizeOptions& options, std::string& error) = 0;
  virtual WriterStatus status() const = 0;
};

enum class EnqueueResult {
  Accepted,
  Stopping,
  Overflow,
  Error,
};

struct StorageStatus {
  WriterStatus raw_writer;
  WriterStatus processed_writer;
  std::size_t raw_queue_size = 0;
  std::size_t raw_queue_capacity = 0;
  std::size_t raw_queue_high_water_mark = 0;
  std::size_t processed_queue_size = 0;
  std::size_t processed_queue_capacity = 0;
  std::size_t processed_queue_high_water_mark = 0;
  std::size_t queue_size = 0;
  std::size_t queue_capacity = 0;
  std::size_t queue_high_water_mark = 0;
  QueueOverflowPolicy overflow_policy = QueueOverflowPolicy::StopAcquisition;
  bool stop_requested = false;
  bool failed = false;
  std::uint64_t last_accepted_frame_id = 0;
  std::uint64_t last_accepted_raw_block = 0;
  std::string stop_reason;
};

class IStorageService {
 public:
  virtual ~IStorageService() = default;
  virtual bool start(const WriterOpenOptions& options, std::string& error) = 0;
  virtual EnqueueResult enqueueRawBatch(RawFrameBatchPtr batch, std::string& error) = 0;
  virtual EnqueueResult enqueueRaw(RawFramePtr frame, std::string& error) = 0;
  virtual EnqueueResult enqueueProcessed(ProcessedFramePtr frame, std::string& error) = 0;
  virtual void requestStop(std::string reason) = 0;
  virtual bool waitUntilStopped(std::string& error) = 0;
  virtual StorageStatus status() const = 0;
};

}  // namespace fmcw
