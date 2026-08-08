#include "fmcw_lidar_rviz/udp_point_protocol.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace fmcw_lidar_rviz {
namespace {

std::vector<PointXYZIV> samplePoints() {
  std::vector<PointXYZIV> points;
  for (int index = 0; index < 5; ++index) {
    PointXYZIV point;
    point.x = static_cast<float>(index);
    point.y = static_cast<float>(index + 1);
    point.z = static_cast<float>(index + 2);
    point.intensity = -30.0F + static_cast<float>(index);
    point.velocity = 0.25F * static_cast<float>(index);
    points.push_back(point);
  }
  return points;
}

std::vector<std::vector<std::uint8_t>> encoded(const std::vector<PointXYZIV>& points,
                                                std::size_t points_per_packet = 2U) {
  std::vector<std::vector<std::uint8_t>> datagrams;
  std::string error;
  EXPECT_TRUE(encodeFrame(7U, 123456U, 11U, points, points_per_packet,
                          datagrams, error)) << error;
  return datagrams;
}

Packet decoded(const std::vector<std::uint8_t>& datagram) {
  Packet packet;
  std::string error;
  EXPECT_TRUE(decodePacket(datagram.data(), datagram.size(), packet, error)) << error;
  return packet;
}

PointXYZIV pointAt(const CompletedFrame& frame, std::size_t index) {
  PointXYZIV point;
  const auto* source = frame.point_data.data() + index * kPointStrideBytes;
  std::memcpy(&point.x, source + 0U, sizeof(float));
  std::memcpy(&point.y, source + 4U, sizeof(float));
  std::memcpy(&point.z, source + 8U, sizeof(float));
  std::memcpy(&point.intensity, source + 12U, sizeof(float));
  std::memcpy(&point.velocity, source + 16U, sizeof(float));
  return point;
}

TEST(UdpPointProtocol, HeaderMatchesCppSenderContract) {
  const auto datagrams = encoded(samplePoints());
  ASSERT_EQ(3U, datagrams.size());
  EXPECT_EQ(80U, datagrams.front().size());
  EXPECT_EQ('F', datagrams.front()[0]);
  EXPECT_EQ('M', datagrams.front()[1]);
  EXPECT_EQ('C', datagrams.front()[2]);
  EXPECT_EQ('W', datagrams.front()[3]);
  EXPECT_EQ(kProtocolVersion, static_cast<std::uint16_t>(datagrams.front()[4]) |
                                  (static_cast<std::uint16_t>(datagrams.front()[5]) << 8U));

  const auto packet = decoded(datagrams[1]);
  EXPECT_EQ(7U, packet.raster_frame_id);
  EXPECT_EQ(123456U, packet.timestamp_ns);
  EXPECT_EQ(11U, packet.config_revision);
  EXPECT_EQ(3U, packet.segment_count);
  EXPECT_EQ(1U, packet.segment_index);
  EXPECT_EQ(2U, packet.point_count);
}

TEST(UdpPointProtocol, ReassemblesOutOfOrderSegmentsInIndexOrder) {
  const auto points = samplePoints();
  const auto datagrams = encoded(points);
  FrameAssembler assembler(std::chrono::milliseconds(500), 8U, 64U, 100U);
  const auto start = FrameAssembler::TimePoint{};
  CompletedFrame completed;
  EXPECT_FALSE(assembler.addAt(decoded(datagrams[2]), start, completed));
  EXPECT_FALSE(assembler.addAt(decoded(datagrams[0]), start + std::chrono::milliseconds(1), completed));
  EXPECT_TRUE(assembler.addAt(decoded(datagrams[1]), start + std::chrono::milliseconds(2), completed));
  ASSERT_EQ(points.size(), completed.point_count);
  for (std::size_t index = 0U; index < points.size(); ++index) {
    const auto actual = pointAt(completed, index);
    EXPECT_FLOAT_EQ(points[index].x, actual.x);
    EXPECT_FLOAT_EQ(points[index].y, actual.y);
    EXPECT_FLOAT_EQ(points[index].z, actual.z);
    EXPECT_FLOAT_EQ(points[index].intensity, actual.intensity);
    EXPECT_FLOAT_EQ(points[index].velocity, actual.velocity);
  }
}

TEST(UdpPointProtocol, RejectsMalformedDatagrams) {
  auto datagram = encoded(samplePoints())[0];
  datagram[0] = 'N';
  Packet packet;
  std::string error;
  EXPECT_FALSE(decodePacket(datagram.data(), datagram.size(), packet, error));

  datagram = encoded(samplePoints())[0];
  datagram.pop_back();
  EXPECT_FALSE(decodePacket(datagram.data(), datagram.size(), packet, error));

  datagram = encoded(samplePoints())[0];
  datagram[4] = 1U;
  datagram[5] = 0U;
  EXPECT_FALSE(decodePacket(datagram.data(), datagram.size(), packet, error));
}

TEST(UdpPointProtocol, RejectsInconsistentDecodedPayloadBeforeAllocation) {
  auto packet = decoded(encoded(samplePoints())[0]);
  packet.point_data.pop_back();
  FrameAssembler assembler(std::chrono::milliseconds(500), 8U, 64U, 100U);
  CompletedFrame completed;
  EXPECT_FALSE(assembler.add(std::move(packet), completed));
  EXPECT_EQ(1U, assembler.statistics().rejected_segment_layouts);
  EXPECT_EQ(0U, assembler.inflightCount());
}

TEST(UdpPointProtocol, ExpiresDuplicatesAndBoundsPointCount) {
  const auto datagrams = encoded(samplePoints());
  const auto start = FrameAssembler::TimePoint{};
  CompletedFrame completed;
  FrameAssembler assembler(std::chrono::milliseconds(100), 2U, 64U, 3U);
  EXPECT_FALSE(assembler.addAt(decoded(datagrams[0]), start, completed));
  EXPECT_FALSE(assembler.addAt(decoded(datagrams[0]), start + std::chrono::milliseconds(1), completed));
  EXPECT_EQ(1U, assembler.statistics().duplicate_segments);
  EXPECT_FALSE(assembler.addAt(decoded(datagrams[1]), start + std::chrono::milliseconds(2), completed));
  EXPECT_EQ(1U, assembler.statistics().oversized_frames);
  EXPECT_EQ(0U, assembler.inflightCount());

  EXPECT_FALSE(assembler.addAt(decoded(datagrams[0]), start + std::chrono::milliseconds(10), completed));
  EXPECT_EQ(1U, assembler.expireAt(start + std::chrono::milliseconds(111)));
  EXPECT_EQ(1U, assembler.statistics().expired_frames);
}

TEST(UdpPointProtocol, SupportsEmptyFrames) {
  const auto datagrams = encoded({}, 256U);
  ASSERT_EQ(1U, datagrams.size());
  FrameAssembler assembler(std::chrono::milliseconds(500), 8U, 64U, 100U);
  CompletedFrame completed;
  EXPECT_TRUE(assembler.add(decoded(datagrams.front()), completed));
  EXPECT_EQ(0U, completed.point_count);
  EXPECT_TRUE(completed.point_data.empty());
}

}  // namespace
}  // namespace fmcw_lidar_rviz

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
