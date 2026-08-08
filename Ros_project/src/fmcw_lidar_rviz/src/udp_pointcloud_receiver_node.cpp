#include "fmcw_lidar_rviz/udp_point_protocol.hpp"

#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/PointField.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <memory>
#include <utility>
#include <vector>

namespace fmcw_lidar_rviz {
namespace {

sensor_msgs::PointField pointField(const std::string& name, std::uint32_t offset) {
  sensor_msgs::PointField field;
  field.name = name;
  field.offset = offset;
  field.datatype = sensor_msgs::PointField::FLOAT32;
  field.count = 1U;
  return field;
}

class SocketHandle final {
 public:
  explicit SocketHandle(int descriptor = -1) : descriptor_(descriptor) {}
  ~SocketHandle() {
    if (descriptor_ >= 0) {
      ::close(descriptor_);
    }
  }
  SocketHandle(const SocketHandle&) = delete;
  SocketHandle& operator=(const SocketHandle&) = delete;
  int get() const { return descriptor_; }

 private:
  int descriptor_ = -1;
};

class UdpPointCloudReceiver final {
 public:
  UdpPointCloudReceiver()
      : private_node_("~"),
        assembler_(readTimeout(), readPositiveSize("max_inflight_frames", 8),
                   readPositiveSize("max_segments_per_frame", 4096),
                   readPositiveSize("max_frame_points", 2000000)) {
    private_node_.param<std::string>("bind_address", bind_address_, "0.0.0.0");
    private_node_.param<std::string>("sender_ip", sender_ip_, "");
    private_node_.param<std::string>("topic", topic_, "/fmcw/points");
    private_node_.param<std::string>("frame_id", frame_id_, "fmcw_lidar");
    private_node_.param("port", port_, 9000);
    private_node_.param("publish_queue_size", publish_queue_size_, 1);
    private_node_.param("socket_receive_buffer", socket_receive_buffer_, 4 * 1024 * 1024);
    private_node_.param("statistics_interval", statistics_interval_seconds_, 5.0);
    private_node_.param("frame_reset_timeout", frame_reset_timeout_seconds_, 2.0);

    validateParameters();
    publisher_ = node_.advertise<sensor_msgs::PointCloud2>(
        topic_, static_cast<std::uint32_t>(publish_queue_size_));
    fields_ = {pointField("x", 0U), pointField("y", 4U), pointField("z", 8U),
               pointField("intensity", 12U), pointField("velocity", 16U)};

    const int descriptor = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (descriptor < 0) {
      throw std::runtime_error("unable to create UDP socket: " +
                               std::string(std::strerror(errno)));
    }
    socket_ = std::unique_ptr<SocketHandle>(new SocketHandle(descriptor));
    configureSocket();
    bindSocket();
    configureSenderFilter();
    next_statistics_ = ros::WallTime::now() + ros::WallDuration(statistics_interval_seconds_);

    int actual_receive_buffer = 0;
    socklen_t option_length = sizeof(actual_receive_buffer);
    ::getsockopt(socket_->get(), SOL_SOCKET, SO_RCVBUF, &actual_receive_buffer, &option_length);
    ROS_INFO_STREAM("FMCW C++ UDP receiver listening on " << bind_address_ << ':' << port_
                    << ", sender=" << (sender_ip_.empty() ? "any" : sender_ip_)
                    << ", topic=" << topic_ << ", frame=" << frame_id_
                    << ", receive_buffer=" << actual_receive_buffer << " bytes");
  }

  void run() {
    std::vector<std::uint8_t> buffer(kMaximumUdpPayloadBytes);
    while (ros::ok()) {
      sockaddr_in source{};
      socklen_t source_length = sizeof(source);
      const auto received = ::recvfrom(
          socket_->get(), buffer.data(), buffer.size(), 0,
          reinterpret_cast<sockaddr*>(&source), &source_length);
      const auto now = FrameAssembler::Clock::now();
      if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
          assembler_.expireAt(now);
          reportStatistics();
          ros::spinOnce();
          continue;
        }
        ROS_ERROR_STREAM_THROTTLE(2.0, "UDP receive failed: " << std::strerror(errno));
        continue;
      }

      ++packets_received_;
      if (sender_filter_enabled_ && source.sin_addr.s_addr != sender_address_.s_addr) {
        ++filtered_packets_;
        reportStatistics();
        continue;
      }

      Packet packet;
      std::string error;
      if (!decodePacket(buffer.data(), static_cast<std::size_t>(received), packet, error)) {
        ++invalid_packets_;
        ROS_WARN_STREAM_THROTTLE(2.0, "Rejected FMCW UDP packet: " << error);
        reportStatistics();
        continue;
      }

      CompletedFrame frame;
      if (assembler_.addAt(std::move(packet), now, frame)) {
        publish(std::move(frame), now);
      }
      reportStatistics();
      ros::spinOnce();
    }
  }

 private:
  std::chrono::milliseconds readTimeout() {
    double seconds = 0.5;
    private_node_.param("assembly_timeout", seconds, seconds);
    if (!(seconds > 0.0)) {
      throw std::invalid_argument("~assembly_timeout must be positive");
    }
    return std::chrono::milliseconds(
        std::max<std::int64_t>(1, static_cast<std::int64_t>(seconds * 1000.0)));
  }

  std::size_t readPositiveSize(const std::string& name, int default_value) {
    int value = default_value;
    private_node_.param(name, value, value);
    if (value <= 0) {
      throw std::invalid_argument("~" + name + " must be positive");
    }
    return static_cast<std::size_t>(value);
  }

  void validateParameters() const {
    if (port_ <= 0 || port_ > 65535) {
      throw std::invalid_argument("~port must be in [1, 65535]");
    }
    if (frame_id_.empty() || topic_.empty()) {
      throw std::invalid_argument("~frame_id and ~topic must not be empty");
    }
    if (publish_queue_size_ <= 0 || socket_receive_buffer_ <= 0 ||
        !(statistics_interval_seconds_ > 0.0) || !(frame_reset_timeout_seconds_ > 0.0)) {
      throw std::invalid_argument("queue, buffer, and timeout parameters must be positive");
    }
  }

  void configureSocket() {
    if (::setsockopt(socket_->get(), SOL_SOCKET, SO_RCVBUF, &socket_receive_buffer_,
                     sizeof(socket_receive_buffer_)) != 0) {
      throw std::runtime_error("unable to set UDP receive buffer: " +
                               std::string(std::strerror(errno)));
    }
    timeval receive_timeout{};
    receive_timeout.tv_sec = 0;
    receive_timeout.tv_usec = 100000;
    if (::setsockopt(socket_->get(), SOL_SOCKET, SO_RCVTIMEO, &receive_timeout,
                     sizeof(receive_timeout)) != 0) {
      throw std::runtime_error("unable to set UDP receive timeout: " +
                               std::string(std::strerror(errno)));
    }
  }

  void bindSocket() {
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<std::uint16_t>(port_));
    if (::inet_pton(AF_INET, bind_address_.c_str(), &local.sin_addr) != 1) {
      throw std::invalid_argument("~bind_address is not a valid IPv4 address");
    }
    if (::bind(socket_->get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
      throw std::runtime_error("unable to bind UDP socket: " +
                               std::string(std::strerror(errno)));
    }
  }

  void configureSenderFilter() {
    if (sender_ip_.empty()) {
      return;
    }
    if (::inet_pton(AF_INET, sender_ip_.c_str(), &sender_address_) != 1) {
      throw std::invalid_argument("~sender_ip is not a valid IPv4 address");
    }
    sender_filter_enabled_ = true;
  }

  void publish(CompletedFrame frame, FrameAssembler::TimePoint now) {
    if (last_frame_valid_ && frame.raster_frame_id <= last_frame_id_) {
      const auto since_last = std::chrono::duration<double>(now - last_publish_time_).count();
      if (since_last < frame_reset_timeout_seconds_) {
        ++stale_frames_;
        return;
      }
      ROS_WARN_STREAM("Accepting FMCW frame counter reset: previous=" << last_frame_id_
                      << ", new=" << frame.raster_frame_id);
    }

    sensor_msgs::PointCloud2 cloud;
    cloud.header.seq = static_cast<std::uint32_t>(frame.raster_frame_id & 0xFFFFFFFFU);
    // The sender uses steady_clock; it has no ROS epoch. Arrival time is TF-safe.
    cloud.header.stamp = ros::Time::now();
    cloud.header.frame_id = frame_id_;
    cloud.height = 1U;
    cloud.width = static_cast<std::uint32_t>(frame.point_count);
    cloud.fields = fields_;
    cloud.is_bigendian = false;
    cloud.point_step = static_cast<std::uint32_t>(kPointStrideBytes);
    cloud.row_step = cloud.point_step * cloud.width;
    cloud.data = std::move(frame.point_data);
    cloud.is_dense = false;
    publisher_.publish(cloud);

    last_frame_valid_ = true;
    last_frame_id_ = frame.raster_frame_id;
    last_publish_time_ = now;
    ++frames_published_;
    points_published_ += frame.point_count;
  }

  void reportStatistics() {
    const auto wall_now = ros::WallTime::now();
    if (wall_now < next_statistics_) {
      return;
    }
    const auto& stats = assembler_.statistics();
    ROS_INFO_STREAM("FMCW UDP stats: packets=" << packets_received_
                    << " invalid=" << invalid_packets_ << " filtered=" << filtered_packets_
                    << " frames=" << frames_published_ << " points=" << points_published_
                    << " stale=" << stale_frames_ << " inflight=" << assembler_.inflightCount()
                    << " expired=" << stats.expired_frames << " evicted=" << stats.evicted_frames
                    << " duplicates=" << stats.duplicate_segments
                    << " inconsistent=" << stats.inconsistent_frames
                    << " oversized=" << stats.oversized_frames
                    << " bad_layout=" << stats.rejected_segment_layouts);
    next_statistics_ = wall_now + ros::WallDuration(statistics_interval_seconds_);
  }

  ros::NodeHandle node_;
  ros::NodeHandle private_node_;
  ros::Publisher publisher_;
  FrameAssembler assembler_;
  std::unique_ptr<SocketHandle> socket_;
  std::vector<sensor_msgs::PointField> fields_;
  std::string bind_address_;
  std::string sender_ip_;
  std::string topic_;
  std::string frame_id_;
  int port_ = 9000;
  int publish_queue_size_ = 1;
  int socket_receive_buffer_ = 4 * 1024 * 1024;
  double statistics_interval_seconds_ = 5.0;
  double frame_reset_timeout_seconds_ = 2.0;
  in_addr sender_address_{};
  bool sender_filter_enabled_ = false;
  bool last_frame_valid_ = false;
  std::uint64_t last_frame_id_ = 0U;
  FrameAssembler::TimePoint last_publish_time_{};
  ros::WallTime next_statistics_{};
  std::uint64_t packets_received_ = 0U;
  std::uint64_t invalid_packets_ = 0U;
  std::uint64_t filtered_packets_ = 0U;
  std::uint64_t frames_published_ = 0U;
  std::uint64_t stale_frames_ = 0U;
  std::uint64_t points_published_ = 0U;
};

}  // namespace
}  // namespace fmcw_lidar_rviz

int main(int argc, char** argv) {
  ros::init(argc, argv, "fmcw_udp_pointcloud_receiver");
  try {
    fmcw_lidar_rviz::UdpPointCloudReceiver receiver;
    receiver.run();
  } catch (const std::exception& exception) {
    ROS_FATAL_STREAM("Unable to start FMCW UDP receiver: " << exception.what());
    return 1;
  }
  return 0;
}
