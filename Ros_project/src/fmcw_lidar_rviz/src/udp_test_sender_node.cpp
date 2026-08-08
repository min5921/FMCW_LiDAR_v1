#include "fmcw_lidar_rviz/udp_point_protocol.hpp"

#include <ros/ros.h>

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace fmcw_lidar_rviz {
namespace {

std::vector<PointXYZIV> buildPoints(int width, int height, double phase) {
  std::vector<PointXYZIV> points;
  points.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int row = 0; row < height; ++row) {
    const auto z = static_cast<float>((row - (height - 1) * 0.5) * 0.08);
    for (int column = 0; column < width; ++column) {
      const auto y = static_cast<float>((column - (width - 1) * 0.5) * 0.08);
      PointXYZIV point;
      point.x = static_cast<float>(4.0 + 0.35 * std::sin(y * 1.7 + phase) +
                                   0.15 * std::cos(z * 3.0));
      point.y = y;
      point.z = z;
      point.intensity = static_cast<float>(50.0 + 45.0 *
          std::sin(column * 0.12 + phase));
      point.velocity = static_cast<float>(0.75 *
          std::cos(row * 0.18 + phase));
      points.push_back(point);
    }
  }
  return points;
}

std::uint64_t steadyNowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

class TestSender final {
 public:
  TestSender() : private_node_("~") {
    private_node_.param<std::string>("ip", ip_, "127.0.0.1");
    private_node_.param("port", port_, 9000);
    private_node_.param("rate", rate_hz_, 10.0);
    private_node_.param("width", width_, 64);
    private_node_.param("height", height_, 32);
    private_node_.param("points_per_packet", points_per_packet_, 256);
    private_node_.param("frame_count", frame_count_, 0);
    validate();

    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ < 0) {
      throw std::runtime_error("unable to create UDP socket: " +
                               std::string(std::strerror(errno)));
    }
    destination_.sin_family = AF_INET;
    destination_.sin_port = htons(static_cast<std::uint16_t>(port_));
    if (::inet_pton(AF_INET, ip_.c_str(), &destination_.sin_addr) != 1) {
      throw std::invalid_argument("~ip is not a valid IPv4 address");
    }
  }

  ~TestSender() {
    if (socket_ >= 0) {
      ::close(socket_);
    }
  }

  void run() {
    ROS_INFO_STREAM("Sending " << width_ << 'x' << height_ << " ROS-axis FMCW test frames to "
                    << ip_ << ':' << port_ << " at " << rate_hz_ << " Hz");
    ros::Rate rate(rate_hz_);
    std::uint64_t frame_id = 1U;
    while (ros::ok() && (frame_count_ == 0 || frame_id <= static_cast<std::uint64_t>(frame_count_))) {
      const auto points = buildPoints(width_, height_, frame_id * 0.08);
      std::vector<std::vector<std::uint8_t>> datagrams;
      std::string error;
      if (!encodeFrame(frame_id, steadyNowNs(), 1U, points,
                       static_cast<std::size_t>(points_per_packet_), datagrams, error)) {
        throw std::runtime_error(error);
      }
      for (const auto& datagram : datagrams) {
        const auto sent = ::sendto(socket_, datagram.data(), datagram.size(), 0,
            reinterpret_cast<const sockaddr*>(&destination_), sizeof(destination_));
        if (sent < 0 || static_cast<std::size_t>(sent) != datagram.size()) {
          throw std::runtime_error("UDP send failed: " +
                                   std::string(std::strerror(errno)));
        }
      }
      ++frame_id;
      ros::spinOnce();
      rate.sleep();
    }
  }

 private:
  void validate() const {
    const auto point_count = static_cast<std::uint64_t>(width_) *
                             static_cast<std::uint64_t>(height_);
    if (port_ <= 0 || port_ > 65535 || !(rate_hz_ > 0.0) || width_ <= 0 ||
        height_ <= 0 || points_per_packet_ <= 0 ||
        static_cast<std::size_t>(points_per_packet_) > kMaximumPointsPerPacket ||
        point_count > 2000000U || frame_count_ < 0) {
      throw std::invalid_argument("test sender parameters are outside valid ranges");
    }
  }

  ros::NodeHandle private_node_;
  std::string ip_;
  int port_ = 9000;
  double rate_hz_ = 10.0;
  int width_ = 64;
  int height_ = 32;
  int points_per_packet_ = 256;
  int frame_count_ = 0;
  int socket_ = -1;
  sockaddr_in destination_{};
};

}  // namespace
}  // namespace fmcw_lidar_rviz

int main(int argc, char** argv) {
  ros::init(argc, argv, "fmcw_udp_test_sender");
  try {
    fmcw_lidar_rviz::TestSender sender;
    sender.run();
  } catch (const std::exception& exception) {
    ROS_FATAL_STREAM("FMCW test sender failed: " << exception.what());
    return 1;
  }
  return 0;
}
