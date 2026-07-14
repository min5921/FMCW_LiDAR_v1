#pragma once

#include "core/config_types.h"
#include "core/frame_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace fmcw {

struct UdpSenderStatus {
  bool running = false;
  bool accepting = false;
  std::uint64_t frames_completed = 0;
  std::uint64_t frames_sent = 0;
  std::uint64_t packets_sent = 0;
  std::uint64_t bytes_sent = 0;
  std::uint64_t dropped_frames = 0;
  std::uint64_t send_errors = 0;
  std::size_t queue_size = 0;
  std::size_t queue_capacity = 0;
  double send_fps = 0.0;
  std::string detail;
};

class UdpSenderService final {
 public:
  UdpSenderService();
  ~UdpSenderService();

  UdpSenderService(const UdpSenderService&) = delete;
  UdpSenderService& operator=(const UdpSenderService&) = delete;

  bool start(const UdpConfig& config, std::uint32_t frame_width, std::uint32_t frame_height,
             std::string& error);
  void enqueue(ProcessedFramePtr frame);
  void stop();
  UdpSenderStatus status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fmcw
