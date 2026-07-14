#include "network/udp_sender_service.h"

#include "network/udp_point_protocol.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fmcw {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle socket) { close(socket); }
#endif

}  // namespace

struct UdpSenderService::Impl {
  void resetAssembly(std::uint64_t raster_frame_id) {
    active_raster_frame_id = raster_frame_id;
    assembly_points.assign(expected_points, {});
    assembly_filled.assign(expected_points, 0U);
    assembly_count = 0;
    assembly_timestamp_ns = 0;
    assembly_config_revision = 0;
    has_active_frame = true;
  }

  void queueCompletedFrame(UdpPointFrame frame) {
    ++sender_status.frames_completed;
    if (queue.size() >= sender_status.queue_capacity) {
      switch (config.backpressure_policy) {
        case UdpBackpressurePolicy::LatestFrame:
          sender_status.dropped_frames += queue.size();
          queue.clear();
          break;
        case UdpBackpressurePolicy::PreserveFrames:
          ++sender_status.dropped_frames;
          return;
        case UdpBackpressurePolicy::StopSending:
          ++sender_status.dropped_frames;
          accepting = false;
          sender_status.accepting = false;
          sender_status.detail = "UDP sending stopped after queue overflow";
          condition.notify_all();
          return;
      }
    }
    queue.push_back(std::move(frame));
    sender_status.queue_size = queue.size();
    condition.notify_one();
  }

  void workerLoop() {
    const auto started_at = std::chrono::steady_clock::now();
    while (true) {
      UdpPointFrame frame;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return !queue.empty() || !accepting; });
        if (queue.empty() && !accepting) {
          break;
        }
        frame = std::move(queue.front());
        queue.pop_front();
        sender_status.queue_size = queue.size();
      }

      std::string error;
      const auto packets = UdpPointProtocol::encodeFrame(
          frame, config.packet_point_count, static_cast<std::uint16_t>(config.packet_format_version), error);
      bool frame_sent = !packets.empty();
      for (const auto& packet : packets) {
        const auto sent = sendto(socket, reinterpret_cast<const char*>(packet.data()),
                                 static_cast<int>(packet.size()), 0,
                                 reinterpret_cast<const sockaddr*>(&target), sizeof(target));
        if (sent != static_cast<int>(packet.size())) {
          frame_sent = false;
          std::lock_guard<std::mutex> lock(mutex);
          ++sender_status.send_errors;
          sender_status.detail = "UDP sendto failed or returned a partial datagram";
          break;
        }
        std::lock_guard<std::mutex> lock(mutex);
        ++sender_status.packets_sent;
        sender_status.bytes_sent += packet.size();
      }
      std::lock_guard<std::mutex> lock(mutex);
      if (!error.empty()) {
        ++sender_status.send_errors;
        sender_status.detail = error;
      } else if (frame_sent) {
        ++sender_status.frames_sent;
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
        sender_status.send_fps = elapsed > 0.0 ? static_cast<double>(sender_status.frames_sent) / elapsed : 0.0;
        sender_status.detail = "UDP point frames are being transmitted";
      }
    }
    std::lock_guard<std::mutex> lock(mutex);
    sender_status.running = false;
    sender_status.accepting = false;
  }

  mutable std::mutex mutex;
  std::condition_variable condition;
  std::deque<UdpPointFrame> queue;
  std::thread worker;
  UdpConfig config;
  UdpSenderStatus sender_status;
  SocketHandle socket = kInvalidSocket;
  sockaddr_in target{};
  std::size_t expected_points = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t active_raster_frame_id = 0;
  std::uint64_t assembly_timestamp_ns = 0;
  std::uint64_t assembly_config_revision = 0;
  std::size_t assembly_count = 0;
  std::vector<PointXYZI> assembly_points;
  std::vector<std::uint8_t> assembly_filled;
  bool has_active_frame = false;
  bool accepting = false;
#if defined(_WIN32)
  bool winsock_started = false;
#endif
};

UdpSenderService::UdpSenderService() : impl_(std::make_unique<Impl>()) {}

UdpSenderService::~UdpSenderService() { stop(); }

bool UdpSenderService::start(const UdpConfig& config, std::uint32_t frame_width,
                             std::uint32_t frame_height, std::string& error) {
  stop();
  if (!config.enabled || frame_width == 0U || frame_height == 0U || config.queue_capacity == 0U ||
      config.target_port == 0U || config.packet_format_version != kUdpPointPacketVersion) {
    error = "UDP sender configuration is incomplete or unsupported";
    return false;
  }
  const auto expected_points = static_cast<std::uint64_t>(frame_width) * frame_height;
  if (expected_points > std::numeric_limits<std::size_t>::max()) {
    error = "UDP raster dimensions are too large";
    return false;
  }
#if defined(_WIN32)
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    error = "WSAStartup failed for UDP sender";
    return false;
  }
  impl_->winsock_started = true;
#endif
  impl_->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (impl_->socket == kInvalidSocket) {
    error = "Unable to create UDP socket";
    stop();
    return false;
  }
  impl_->target = {};
  impl_->target.sin_family = AF_INET;
  impl_->target.sin_port = htons(config.target_port);
  if (inet_pton(AF_INET, config.target_ip.c_str(), &impl_->target.sin_addr) != 1) {
    error = "UDP target IPv4 address is invalid";
    stop();
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->config = config;
    impl_->width = frame_width;
    impl_->height = frame_height;
    impl_->expected_points = static_cast<std::size_t>(expected_points);
    impl_->queue.clear();
    impl_->has_active_frame = false;
    impl_->accepting = true;
    impl_->sender_status = {};
    impl_->sender_status.running = true;
    impl_->sender_status.accepting = true;
    impl_->sender_status.queue_capacity = config.queue_capacity;
    impl_->sender_status.detail = "UDP sender ready";
  }
  impl_->worker = std::thread([impl = impl_.get()] { impl->workerLoop(); });
  error.clear();
  return true;
}

void UdpSenderService::enqueue(ProcessedFramePtr frame) {
  if (!frame || !frame->scan_position.valid) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->accepting || frame->scan_position.x_index >= impl_->width ||
      frame->scan_position.y_index >= impl_->height || impl_->expected_points == 0U) {
    return;
  }
  const auto zero_based_frame = frame->frame_id == 0U ? 0U : frame->frame_id - 1U;
  const auto raster_frame_id = zero_based_frame / impl_->expected_points;
  if (!impl_->has_active_frame || impl_->active_raster_frame_id != raster_frame_id) {
    if (impl_->has_active_frame && impl_->assembly_count != impl_->expected_points) {
      ++impl_->sender_status.dropped_frames;
    }
    impl_->resetAssembly(raster_frame_id);
  }
  const auto offset = static_cast<std::size_t>(frame->scan_position.y_index) * impl_->width +
                      frame->scan_position.x_index;
  if (impl_->assembly_filled[offset] == 0U) {
    impl_->assembly_filled[offset] = 1U;
    ++impl_->assembly_count;
  }
  impl_->assembly_points[offset] = frame->point;
  impl_->assembly_timestamp_ns = frame->source_timestamp_ns;
  impl_->assembly_config_revision = frame->config_revision;
  if (impl_->assembly_count != impl_->expected_points) {
    return;
  }
  UdpPointFrame completed;
  completed.raster_frame_id = raster_frame_id + 1U;
  completed.timestamp_ns = impl_->assembly_timestamp_ns;
  completed.config_revision = impl_->assembly_config_revision;
  completed.points.reserve(impl_->expected_points);
  std::copy_if(impl_->assembly_points.begin(), impl_->assembly_points.end(),
               std::back_inserter(completed.points), [](const PointXYZI& point) { return point.valid; });
  impl_->queueCompletedFrame(std::move(completed));
  impl_->has_active_frame = false;
}

void UdpSenderService::stop() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->accepting = false;
    impl_->sender_status.accepting = false;
    impl_->condition.notify_all();
  }
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  if (impl_->socket != kInvalidSocket) {
    closeSocket(impl_->socket);
    impl_->socket = kInvalidSocket;
  }
#if defined(_WIN32)
  if (impl_->winsock_started) {
    WSACleanup();
    impl_->winsock_started = false;
  }
#endif
}

UdpSenderStatus UdpSenderService::status() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  auto status = impl_->sender_status;
  status.queue_size = impl_->queue.size();
  return status;
}

}  // namespace fmcw
