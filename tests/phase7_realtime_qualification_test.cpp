#include "core/acquisition_session.h"
#include "core/continuous_acquisition_worker.h"
#include "drivers/simulator/fake_digitizer.h"
#include "drivers/simulator/fake_edfa.h"
#include "drivers/simulator/fake_mcu.h"
#include "processing/fft_backends.h"
#include "processing/processing_service.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#ifndef FMCW_QUALIFICATION_DURATION_SECONDS
#define FMCW_QUALIFICATION_DURATION_SECONDS 2
#endif

#ifndef FMCW_QUALIFICATION_ENFORCE_HARD_PASS
#define FMCW_QUALIFICATION_ENFORCE_HARD_PASS 0
#endif

namespace {

constexpr auto kRunDuration = std::chrono::seconds(FMCW_QUALIFICATION_DURATION_SECONDS);

struct QualificationResult {
  bool functional = false;
  bool hard_pass = false;
};

QualificationResult runQualification(fmcw::FftBackendKind backend_kind) {
  auto config = fmcw::makeAts9371QualificationSimulatorConfig();
  config.processing.fft_backend = backend_kind;

  fmcw::FakeDigitizer digitizer;
  fmcw::FakeEdfaController edfa;
  fmcw::FakeMcuController mcu;
  fmcw::AcquisitionSession session(digitizer, edfa, mcu);
  fmcw::ProcessingService processing(fmcw::createFftBackend(backend_kind));
  fmcw::ContinuousAcquisitionWorker acquisition(session);

  std::atomic<std::uint64_t> callback_count{0U};
  std::atomic<std::uint64_t> valid_point_count{0U};
  std::atomic_bool payload_shape_valid{true};
  std::atomic<std::uint64_t> rejected_batches{0U};
  std::mutex exit_mutex;
  std::condition_variable exit_condition;
  bool acquisition_exited = false;
  bool acquisition_failed = false;
  std::string acquisition_exit_reason;
  std::string error;

  processing.setProcessedFrameCallback([&](fmcw::ProcessedFramePtr frame) {
    ++callback_count;
    if (frame && frame->measurement_valid && frame->point.valid &&
        std::isfinite(frame->point.x) && std::isfinite(frame->point.y) &&
        std::isfinite(frame->point.z) && std::isfinite(frame->point.intensity) &&
        std::isfinite(frame->point.velocity)) {
      ++valid_point_count;
    }
  });

  const auto backend_name = backend_kind == fmcw::FftBackendKind::Cuda ? "CUDA" : "FFTW";
  if (!processing.configure(config, 1U, error) || !processing.start(error) ||
      !session.configure(config, 1U, error) || !session.connect(error) || !session.start(error)) {
    std::cerr << "phase7_3d backend=" << backend_name << " setup_error=\"" << error << "\"\n";
    processing.requestStop("Qualification setup failed");
    std::string ignored;
    processing.waitUntilStopped(ignored);
    return {};
  }

  if (!acquisition.start(
          [&](fmcw::RawFrameBatchPtr batch, std::string& batch_error) {
            const bool valid_shape = batch && batch->records.size() == 998U &&
                batch->metadata.record_count == 998U && batch->metadata.record_length == 4992U &&
                batch->records.front().samples.size() == 4992U &&
                batch->records.back().samples.size() == 4992U;
            if (!valid_shape) {
              payload_shape_valid.store(false);
              batch_error = "Strict simulator produced an invalid DMA payload shape";
              ++rejected_batches;
              return false;
            }
            const auto result = processing.enqueueBatch(std::move(batch), batch_error);
            if (result != fmcw::ProcessingEnqueueResult::Accepted) {
              ++rejected_batches;
              return false;
            }
            return true;
          },
          [&](bool failed, std::string reason) {
            std::lock_guard<std::mutex> lock(exit_mutex);
            acquisition_failed = failed;
            acquisition_exit_reason = std::move(reason);
            acquisition_exited = true;
            exit_condition.notify_all();
          },
          error)) {
    std::cerr << "phase7_3d backend=" << backend_name << " worker_error=\"" << error << "\"\n";
    session.stop(error);
    processing.requestStop("Qualification worker failed to start");
    processing.waitUntilStopped(error);
    return {};
  }

  bool completed_duration = false;
  {
    std::unique_lock<std::mutex> lock(exit_mutex);
    completed_duration = !exit_condition.wait_for(lock, kRunDuration, [&] { return acquisition_exited; });
  }

  acquisition.requestStop();
  std::string session_stop_error;
  const bool session_stopped = session.stop(session_stop_error);
  std::string worker_stop_error;
  const bool worker_stopped_cleanly = acquisition.waitUntilStopped(worker_stop_error);
  processing.requestStop("Phase 7.3D qualification complete");
  std::string processing_stop_error;
  const bool processing_stopped = processing.waitUntilStopped(processing_stop_error);

  const auto acquisition_status = acquisition.status();
  const auto digitizer_status = digitizer.telemetry();
  const auto processing_status = processing.status();
  const auto bscan = processing.snapshots().latestBScan();
  const auto expected_processed_records = processing_status.batches_processed * 998U;
  const bool exact_results = processing_status.frames_processed == expected_processed_records &&
      callback_count.load() == expected_processed_records &&
      valid_point_count.load() == expected_processed_records;
  const bool bscan_complete = processing_status.batches_processed == 0U ||
      (bscan && bscan->width == 998U && bscan->height == config.scan.y_line_count &&
       bscan->completed_lines >= 1U && bscan->completed_lines <= bscan->height &&
       bscan->depth_m.size() == static_cast<std::size_t>(bscan->width) * bscan->height);
  const bool overflow_reported = acquisition_status.failed &&
      (acquisition_status.stop_reason.find("overflow") != std::string::npos ||
       acquisition_status.stop_reason.find("capacity exceeded") != std::string::npos);
  const bool stop_route_valid = completed_duration || overflow_reported;
  const bool functional = payload_shape_valid.load() && session_stopped && processing_stopped &&
      exact_results && bscan_complete && stop_route_valid && processing_status.batches_processed > 0U &&
      (worker_stopped_cleanly || overflow_reported);
  const bool hard_pass = functional && completed_duration && !acquisition_failed &&
      rejected_batches.load() == 0U && digitizer_status.dma_buffer_drops == 0U &&
      acquisition_status.batches_delivered == processing_status.batches_processed &&
      processing_status.queue_size == 0U && processing_status.batch_deadline_misses == 0U &&
      processing_status.maximum_batch_latency_ms <= processing_status.batch_deadline_ms;

  std::cout << "phase7_3d backend=" << backend_name
            << " duration_s=" << FMCW_QUALIFICATION_DURATION_SECONDS
            << " result=" << (hard_pass ? "HARD_PASS" : "HARD_FAIL")
            << " functional=" << (functional ? "PASS" : "FAIL")
            << " completed_duration=" << completed_duration
            << " delivered_batches=" << acquisition_status.batches_delivered
            << " processed_batches=" << processing_status.batches_processed
            << " processed_records=" << processing_status.frames_processed
            << " valid_xyziv=" << valid_point_count.load()
            << " queue_high_water=" << processing_status.queue_high_water_mark
            << "/" << processing_status.queue_capacity
            << " dma_drops=" << digitizer_status.dma_buffer_drops
            << " rejected_batches=" << rejected_batches.load()
            << " p50_ms=" << processing_status.batch_latency_p50_ms
            << " p95_ms=" << processing_status.batch_latency_p95_ms
            << " p99_ms=" << processing_status.batch_latency_p99_ms
            << " max_ms=" << processing_status.maximum_batch_latency_ms
            << " deadline_misses=" << processing_status.batch_deadline_misses
            << " stop_reason=\"" << acquisition_status.stop_reason << "\"";
  if (!session_stop_error.empty()) {
    std::cout << " session_stop_error=\"" << session_stop_error << "\"";
  }
  if (!worker_stop_error.empty() && !overflow_reported) {
    std::cout << " worker_stop_error=\"" << worker_stop_error << "\"";
  }
  if (!processing_stop_error.empty()) {
    std::cout << " processing_stop_error=\"" << processing_stop_error << "\"";
  }
  std::cout << '\n';
  return {functional, hard_pass};
}

}  // namespace

int main() {
  const auto fftw = runQualification(fmcw::FftBackendKind::Fftw);
  bool cuda_available = fmcw::CudaFftBackend::available();
  QualificationResult cuda;
  if (cuda_available) {
    cuda = runQualification(fmcw::FftBackendKind::Cuda);
  } else {
    std::cout << "phase7_3d backend=CUDA result=SKIP reason=\"No runtime CUDA device\"\n";
  }

  const bool functional = fftw.functional && (!cuda_available || cuda.functional);
#if FMCW_QUALIFICATION_ENFORCE_HARD_PASS
  const bool accepted = functional && fftw.hard_pass && cuda_available && cuda.hard_pass;
#else
  const bool accepted = functional;
#endif
  return accepted ? 0 : 1;
}
