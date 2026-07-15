#include "network/udp_point_protocol.h"
#include "network/udp_sender_service.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void testPacketRoundTrip() {
  fmcw::UdpPointFrame frame;
  frame.raster_frame_id = 7;
  frame.timestamp_ns = 123456;
  frame.config_revision = 11;
  for (int index = 0; index < 5; ++index) {
    fmcw::PointXYZI point;
    point.x = static_cast<float>(index);
    point.y = static_cast<float>(index + 1);
    point.z = static_cast<float>(index + 2);
    point.intensity = -30.0F + static_cast<float>(index);
    point.velocity = 0.25F * static_cast<float>(index);
    point.valid = true;
    frame.points.push_back(point);
  }
  std::string error;
  const auto packets = fmcw::UdpPointProtocol::encodeFrame(frame, 2, 1, error);
  expect(error.empty() && packets.size() == 3U, "five points split into three UDP segments");
  fmcw::UdpPointPacket decoded;
  expect(fmcw::UdpPointProtocol::decodePacket(packets.at(1), decoded, error),
         "UDP point packet decodes");
  expect(decoded.raster_frame_id == 7U && decoded.segment_count == 3U &&
             decoded.segment_index == 1U && decoded.points.size() == 2U &&
             std::abs(decoded.points.front().x - 2.0F) < 1.0e-6F &&
             std::abs(decoded.points.front().y - 3.0F) < 1.0e-6F &&
             std::abs(decoded.points.front().z - 4.0F) < 1.0e-6F &&
             std::abs(decoded.points.front().intensity + 28.0F) < 1.0e-6F &&
             std::abs(decoded.points.front().velocity - 0.5F) < 1.0e-6F,
         "decoded UDP header and complete XYZIV payload match the source frame");
}

void testAsyncSender() {
  fmcw::UdpConfig config;
  config.enabled = true;
  config.target_ip = "127.0.0.1";
  config.target_port = 49001;
  config.packet_point_count = 2;
  config.packet_format_version = 1;
  config.queue_capacity = 2;
  fmcw::UdpSenderService sender;
  std::string error;
  expect(sender.start(config, 2, 2, error), "asynchronous UDP sender starts");
  for (std::uint64_t index = 0; index < 4; ++index) {
    auto frame = std::make_shared<fmcw::ProcessedFrame>();
    frame->frame_id = index + 1U;
    frame->source_timestamp_ns = 1000U + index;
    frame->config_revision = 3;
    frame->scan_position.x_index = static_cast<std::uint32_t>(index % 2U);
    frame->scan_position.y_index = static_cast<std::uint32_t>(index / 2U);
    frame->scan_position.valid = true;
    frame->point.x = static_cast<float>(index);
    frame->point.y = static_cast<float>(index + 1U);
    frame->point.z = static_cast<float>(index + 2U);
    frame->point.intensity = -30.0F;
    frame->point.velocity = 0.25F;
    frame->point.valid = true;
    sender.enqueue(frame);
  }
  for (int attempt = 0; attempt < 100 && sender.status().frames_sent == 0U; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const auto status = sender.status();
  expect(status.frames_completed == 1U && status.frames_sent == 1U && status.packets_sent == 2U,
         "complete raster frame is packetized and sent on the UDP worker thread");
  sender.stop();
}

}  // namespace

int main() {
  testPacketRoundTrip();
  testAsyncSender();
  if (failures == 0) {
    std::cout << "All Phase 6 UDP tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
