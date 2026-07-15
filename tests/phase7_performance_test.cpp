#include "drivers/simulator/fake_digitizer.h"
#include "processing/fft_backends.h"
#include "processing/processing_service.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::uint64_t nowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

int main() {
  auto config = fmcw::makeAts9371QualificationSimulatorConfig();
  fmcw::FakeDigitizer digitizer;
  std::string error;
  if (!digitizer.configure(config, error) || !digitizer.connect(error) || !digitizer.start(error)) {
    std::cerr << "Qualification simulator setup failed: " << error << '\n';
    return 1;
  }

  fmcw::MutableRawFrameBatchPtr mutable_batch;
  if (digitizer.waitForBatch(mutable_batch, std::chrono::milliseconds(100), error) !=
          fmcw::FrameWaitResult::FrameReady ||
      !mutable_batch || mutable_batch->records.size() != 998U) {
    std::cerr << "Qualification DMA batch failed: " << error << '\n';
    return 1;
  }
  digitizer.stop(error);

  const auto processing_start_ns = nowNs();
  mutable_batch->metadata.completion_timestamp_ns = processing_start_ns;
  mutable_batch->metadata.ownership_ready_timestamp_ns = processing_start_ns;

  fmcw::ProcessingService service(std::make_unique<fmcw::FftwBackend>());
  std::atomic<std::uint64_t> callback_count{0};
  std::atomic<std::uint64_t> valid_point_count{0};
  service.setProcessedFrameCallback([&](fmcw::ProcessedFramePtr frame) {
    ++callback_count;
    if (frame && frame->measurement_valid && frame->point.valid &&
        std::isfinite(frame->point.x) && std::isfinite(frame->point.y) &&
        std::isfinite(frame->point.z) && std::isfinite(frame->point.intensity) &&
        std::isfinite(frame->point.velocity)) {
      ++valid_point_count;
    }
  });
  if (!service.configure(config, 1U, error) || !service.start(error)) {
    std::cerr << "Baseline processing setup failed: " << error << '\n';
    return 1;
  }

  fmcw::RawFrameBatchPtr batch = mutable_batch;
  if (service.enqueueBatch(std::move(batch), error) != fmcw::ProcessingEnqueueResult::Accepted) {
    std::cerr << "Baseline processing enqueue failed: " << error << '\n';
    return 1;
  }
  service.requestStop("Phase 7.3A baseline complete");
  if (!service.waitUntilStopped(error)) {
    std::cerr << "Baseline processing failed: " << error << '\n';
    return 1;
  }

  const auto status = service.status();
  const auto bscan = service.snapshots().latestBScan();
  std::cout << "phase7_3a records=" << status.frames_processed
            << " ffts=" << status.frames_processed * 2U
            << " valid_xyziv=" << valid_point_count.load()
            << " batch_ms=" << status.last_batch_latency_ms
            << " signal_ms=" << status.last_signal_processing_latency_ms
            << " p50_ms=" << status.batch_latency_p50_ms
            << " p95_ms=" << status.batch_latency_p95_ms
            << " p99_ms=" << status.batch_latency_p99_ms
            << " max_ms=" << status.maximum_batch_latency_ms
            << " deadline_misses=" << status.batch_deadline_misses << '\n';

  const bool complete = status.batches_processed == 1U && status.frames_processed == 998U &&
      callback_count.load() == 998U && valid_point_count.load() == 998U && bscan &&
      bscan->width == 998U && bscan->completed_lines == 1U;
  if (!complete) {
    std::cerr << "Phase 7.3A baseline did not produce one complete 998-point B-scan line\n";
    return 1;
  }
  return 0;
}
