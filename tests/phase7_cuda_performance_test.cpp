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
  if (!fmcw::CudaFftBackend::available()) {
    std::cout << "Phase 7.3C CUDA qualification skipped: no runtime CUDA device.\n";
    return 0;
  }
  constexpr std::uint64_t warmup_batch_count = 3U;
  constexpr std::uint64_t measured_batch_count = 32U;
  auto config = fmcw::makeAts9371QualificationSimulatorConfig();
  config.processing.fft_backend = fmcw::FftBackendKind::Cuda;
  fmcw::FakeDigitizer digitizer;
  std::string error;
  if (!digitizer.configure(config, error) || !digitizer.connect(error) || !digitizer.start(error)) {
    std::cerr << "CUDA qualification simulator setup failed: " << error << '\n';
    return 1;
  }

  fmcw::MutableRawFrameBatchPtr mutable_batch;
  if (digitizer.waitForBatch(mutable_batch, std::chrono::milliseconds(100), error) !=
          fmcw::FrameWaitResult::FrameReady ||
      !mutable_batch || mutable_batch->records.size() != 998U) {
    std::cerr << "CUDA qualification DMA batch failed: " << error << '\n';
    return 1;
  }
  digitizer.stop(error);

  fmcw::ProcessingService service(std::make_unique<fmcw::CudaFftBackend>());
  std::atomic<std::uint64_t> callback_count{0U};
  std::atomic<std::uint64_t> valid_point_count{0U};
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
    std::cerr << "CUDA qualification processing setup failed: " << error << '\n';
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

  if (!run_batches(warmup_batch_count)) {
    std::cerr << "CUDA qualification warm-up failed: " << error << '\n';
    return 1;
  }
  service.requestStop("Phase 7.3C warm-up complete");
  if (!service.waitUntilStopped(error)) {
    std::cerr << "CUDA qualification warm-up shutdown failed: " << error << '\n';
    return 1;
  }

  callback_count.store(0U);
  valid_point_count.store(0U);
  if (!service.start(error) || !run_batches(measured_batch_count)) {
    std::cerr << "CUDA steady-state qualification failed: " << error << '\n';
    return 1;
  }
  service.requestStop("Phase 7.3C steady-state measurement complete");
  if (!service.waitUntilStopped(error)) {
    std::cerr << "CUDA qualification shutdown failed: " << error << '\n';
    return 1;
  }

  const auto status = service.status();
  const auto bscan = service.snapshots().latestBScan();
  const auto processor_batch_ms = status.average_latency_ms *
      static_cast<double>(config.digitizer.records_per_buffer);
  std::cout << "phase7_3c batches=" << status.batches_processed
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
    std::cerr << "Phase 7.3C CUDA benchmark did not preserve every 998-point line\n";
    return 1;
  }
  return 0;
}
