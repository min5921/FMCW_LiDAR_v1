#pragma once
#include "storage/point_cloud_replay.h"

namespace fmcw {
// One prefetched frame. Parsing and filesystem I/O never execute on the caller.
class AsyncPointCloudReplay {
 public:
  AsyncPointCloudReplay();
  ~AsyncPointCloudReplay();
  bool open(const std::filesystem::path& path, std::string& error);
  PointCloudReadResult readNext(PointCloudSnapshot& frame, std::string& error);
  bool rewind(std::string& error);
  void close();
  bool isOpen() const;
  PointCloudReplayInfo info() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace fmcw
