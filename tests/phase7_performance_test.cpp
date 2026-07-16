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
  constexpr std::uint64_t measured_batch_count = 32U;
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

  const auto run_batches = [&](std::uint64_t batch_count) {
    for (std::uint64_t batch_index = 0U; batch_index < batch_count; ++batch_index) {
      const auto timestamp_ns = nowNs();
      mutable_batch->metadata.sequence = batch_index;
      mutable_batch->metadata.completion_timestamp_ns = timestamp_ns;
      mutable_batch->metadata.ownership_ready_timestamp_ns = timestamp_ns;
      fmcw::RawFrameBatchPtr batch = mutable_batch;
      if (service.enqueueBatch(std::move(batch), error) !=
          fmcw::ProcessingEnqueueResult::Accepted) {
        return false;
      }
      if (!service.waitForProcessedBatches(batch_index + 1U,
                                           std::chrono::seconds(2), error)) {
        return false;
      }
    }
    return true;
  };

  if (!run_batches(measured_batch_count)) {
    std::cerr << "Qualification processing failed: " << error << '\n';
    return 1;
  }
  service.requestStop("Phase 7.3B measurement complete");
  if (!service.waitUntilStopped(error)) {
    std::cerr << "Qualification shutdown failed: " << error << '\n';
    return 1;
  }

  const auto status = service.status();
  const auto bscan = service.snapshots().latestBScan();
  const auto processor_batch_ms = status.average_latency_ms *
      static_cast<double>(config.digitizer.records_per_buffer);
  std::cout << "phase7_3b batches=" << status.batches_processed
            << " records_per_batch=" << config.digitizer.records_per_buffer
            << " ffts_per_batch=" << config.digitizer.records_per_buffer * 2U
            << " valid_xyziv=" << valid_point_count.load()
            << " processor_average_ms=" << processor_batch_ms
            << " last_end_to_end_ms=" << status.last_batch_latency_ms
            << " p50_ms=" << status.batch_latency_p50_ms
            << " p95_ms=" << status.batch_latency_p95_ms
            << " p99_ms=" << status.batch_latency_p99_ms
            << " max_ms=" << status.maximum_batch_latency_ms
            << " deadline_misses=" << status.batch_deadline_misses << '\n';

  const auto expected_record_count = measured_batch_count *
      static_cast<std::uint64_t>(config.digitizer.records_per_buffer);
  const bool complete = status.batches_processed == measured_batch_count &&
      status.frames_processed == expected_record_count &&
      callback_count.load() == expected_record_count &&
      valid_point_count.load() == expected_record_count && bscan &&
      bscan->width == 998U && bscan->completed_lines == 1U;
  if (!complete) {
    std::cerr << "Phase 7.3B benchmark did not preserve every 998-point B-scan line\n";
    return 1;
  }
  return 0;
}
