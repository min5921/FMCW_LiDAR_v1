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
  std::uint64_t frames_written = 0;
  std::uint64_t dropped_frames = 0;
  double throughput_mbps = 0.0;
  std::string detail;
};

struct SessionDescriptor {
  std::string session_id;
  std::string profile_id;
  std::string platform;
  std::string application_version;
  std::uint32_t config_schema_version = 1;
  std::uint64_t start_timestamp_utc_ns = 0;
  std::string config_snapshot_json;
};

struct RawStreamDescriptor {
  std::uint32_t format_version = kRawFrameFormatVersion;
  DigitizerChannel channel = DigitizerChannel::A;
  SampleFormat sample_format = SampleFormat::SignedInt16;
  ByteOrder byte_order = ByteOrder::LittleEndian;
  double sample_rate_hz = 0.0;
  std::uint32_t record_length = 0;
};

struct WriterOpenOptions {
  std::filesystem::path session_directory;
  std::string file_stem;
  SessionDescriptor session;
  RawStreamDescriptor raw_stream;
  bool raw_enabled = true;
  bool processed_enabled = false;
};

class IRawFrameWriter {
 public:
  virtual ~IRawFrameWriter() = default;
  virtual bool open(const WriterOpenOptions& options, std::string& error) = 0;
  virtual bool write(const RawFrame& frame, std::string& error) = 0;
  virtual bool flush(std::string& error) = 0;
  virtual bool finalize(std::string& error) = 0;
  virtual WriterStatus status() const = 0;
};

class IProcessedFrameWriter {
 public:
  virtual ~IProcessedFrameWriter() = default;
  virtual bool open(const WriterOpenOptions& options, std::string& error) = 0;
  virtual bool write(const ProcessedFrame& frame, std::string& error) = 0;
  virtual bool flush(std::string& error) = 0;
  virtual bool finalize(std::string& error) = 0;
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
  std::size_t queue_size = 0;
  std::size_t queue_capacity = 0;
  std::size_t queue_high_water_mark = 0;
  QueueOverflowPolicy overflow_policy = QueueOverflowPolicy::StopAcquisition;
  bool stop_requested = false;
  std::uint64_t last_accepted_frame_id = 0;
  std::string stop_reason;
};

class IStorageService {
 public:
  virtual ~IStorageService() = default;
  virtual bool start(const WriterOpenOptions& options, std::string& error) = 0;
  virtual EnqueueResult enqueueRaw(RawFramePtr frame, std::string& error) = 0;
  virtual EnqueueResult enqueueProcessed(ProcessedFramePtr frame, std::string& error) = 0;
  virtual void requestStop(std::string reason) = 0;
  virtual bool waitUntilStopped(std::string& error) = 0;
  virtual StorageStatus status() const = 0;
};

}  // namespace fmcw
