#pragma once

#include "processing/processing_snapshots.h"
#include "core/cancellation.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fmcw {

enum class PointCloudReadResult {
  Pending,
  FrameReady,
  EndOfStream,
  Error,
};

enum class PointCloudInputFormat {
  Unknown,
  FmcwBinaryV1,
  WaymoBinaryV2,
  PcdAscii,
  PcdBinary,
  DelimitedText,
};

struct PointCloudReplayInfo {
  PointCloudInputFormat format = PointCloudInputFormat::Unknown;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t point_count = 0U;
  bool organized = false;
  bool has_intensity = false;
  bool has_velocity = false;
  bool has_elongation = false;
  bool multiple_frames = false;
  std::string format_name;
};

class PointCloudReplayReader {
 public:
  PointCloudReplayReader();
  ~PointCloudReplayReader();

  bool open(const std::filesystem::path& path, std::string& error, const CancellationCheck& cancelled = {});
  PointCloudReadResult readNext(PointCloudSnapshot& frame, std::string& error, const CancellationCheck& cancelled = {});
  bool rewind(std::string& error);
  void close();
  bool isOpen() const;
  const PointCloudReplayInfo& info() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fmcw
