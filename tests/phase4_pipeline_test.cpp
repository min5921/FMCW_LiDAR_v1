#include "drivers/simulator/fake_digitizer.h"
#include "drivers/replay/replay_digitizer.h"
#include "processing/fft_backends.h"
#include "processing/processing_service.h"
#include "processing/signal_processor.h"
#include "storage/async_storage_service.h"
#include "storage/binary_storage.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expectNear(double actual, double expected, double tolerance, const std::string& message) {
  expect(std::abs(actual - expected) <= tolerance,
         message + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
}

fmcw::SystemConfig testConfig() {
  fmcw::SystemConfig config;
  config.processing.fft_backend = fmcw::FftBackendKind::Fftw;
  config.processing.peak_threshold_db = -60.0;
  config.processing.queue_capacity = 16;
  config.digitizer.records_per_buffer = 4;
  config.scan.x_pixel_count = 4;
  config.scan.y_line_count = 2;
  config.digitizer.a_scan_count = 4;
  config.digitizer.b_scan_count = 2;
  config.calibration.x_angle_offset_deg = 1.25;
  config.calibration.y_angle_offset_deg = -0.75;
  return config;
}

std::vector<fmcw::RawFramePtr> makeFakeFrames(const fmcw::SystemConfig& config, std::size_t count) {
  fmcw::FakeDigitizer digitizer;
  std::string error;
  expect(digitizer.configure(config, error), "fake digitizer configures for Phase 4 tests");
  expect(digitizer.connect(error), "fake digitizer connects for Phase 4 tests");
  expect(digitizer.start(error), "fake digitizer starts for Phase 4 tests");
  std::vector<fmcw::RawFramePtr> frames;
  for (std::size_t index = 0; index < count; ++index) {
    fmcw::RawFrame frame;
    expect(digitizer.waitForFrame(frame, std::chrono::milliseconds(10), error) ==
               fmcw::FrameWaitResult::FrameReady,
           "fake digitizer produces a Phase 4 frame");
    frame.metadata.config_revision = 1;
    frames.push_back(std::make_shared<const fmcw::RawFrame>(std::move(frame)));
  }
  digitizer.abort(error);
  digitizer.stop(error);
  return frames;
}

void testFftBackends() {
  fmcw::FftwBackend fftw;
  std::string error;
  const std::size_t length = 1024;
  expect(fftw.prepare({length, 1}, error), "FFTW creates a reusable plan");
  std::vector<float> input(length);
  constexpr double pi = 3.14159265358979323846;
  for (std::size_t index = 0; index < length; ++index) {
    input[index] = static_cast<float>(std::sin(2.0 * pi * 29.0 * static_cast<double>(index) /
                                               static_cast<double>(length)));
  }
  std::vector<std::complex<float>> fftw_output;
  expect(fftw.execute(input, fftw_output, error), "FFTW executes the prepared plan");
  const auto peak = std::max_element(fftw_output.begin(), fftw_output.end(),
                                     [](const auto& left, const auto& right) {
                                       return std::abs(left) < std::abs(right);
                                     });
  expect(std::distance(fftw_output.begin(), peak) == 29, "FFTW finds the synthetic tone bin");

  if (fmcw::CudaFftBackend::available()) {
    fmcw::CudaFftBackend cuda;
    std::vector<std::complex<float>> cuda_output;
    expect(cuda.prepare({length, 1}, error) && cuda.execute(input, cuda_output, error),
           "CUDA backend executes when a GPU is available");
    expect(cuda_output.size() == fftw_output.size(), "CUDA and FFTW output sizes match");
    if (cuda_output.size() == fftw_output.size()) {
      double max_error = 0.0;
      for (std::size_t index = 0; index < cuda_output.size(); ++index) {
        max_error = std::max(max_error, static_cast<double>(std::abs(cuda_output[index] - fftw_output[index])));
      }
      expect(max_error < 0.05, "CUDA and FFTW spectra agree within tolerance");
    }
  } else {
    std::cout << "CUDA FFT comparison skipped: no runtime CUDA device.\n";
  }
}

void testSignalProcessingBackendParity(const fmcw::SystemConfig& base_config,
                                       const fmcw::RawFrame& raw) {
  if (!fmcw::CudaFftBackend::available()) {
    std::cout << "Full FFTW/CUDA processing parity skipped: no runtime CUDA device.\n";
    return;
  }

  auto fftw_config = base_config;
  fftw_config.processing.fft_backend = fmcw::FftBackendKind::Fftw;
  auto cuda_config = base_config;
  cuda_config.processing.fft_backend = fmcw::FftBackendKind::Cuda;
  fmcw::SignalProcessor fftw_processor(std::make_unique<fmcw::FftwBackend>());
  fmcw::SignalProcessor cuda_processor(std::make_unique<fmcw::CudaFftBackend>());
  std::string error;
  const bool configured = fftw_processor.configure(fftw_config, 7, error) &&
      cuda_processor.configure(cuda_config, 7, error);
  expect(configured, "FFTW and CUDA processors configure with the same algorithm settings");
  if (!configured) {
    return;
  }

  fmcw::ProcessedFrame fftw_result;
  fmcw::ProcessedFrame cuda_result;
  const bool processed = fftw_processor.process(raw, fftw_result, error) &&
      cuda_processor.process(raw, cuda_result, error);
  expect(processed, "FFTW and CUDA execute the same full signal-processing contract");
  if (!processed) {
    return;
  }

  expect(fftw_result.measurement_valid == cuda_result.measurement_valid &&
             fftw_result.up_peak.valid == cuda_result.up_peak.valid &&
             fftw_result.down_peak.valid == cuda_result.down_peak.valid,
         "FFTW and CUDA produce identical measurement and peak validity");
  expect(fftw_result.up_peak.discrete_bin == cuda_result.up_peak.discrete_bin &&
             fftw_result.down_peak.discrete_bin == cuda_result.down_peak.discrete_bin &&
             fftw_result.up_peak.peak_bin == cuda_result.up_peak.peak_bin &&
             fftw_result.down_peak.peak_bin == cuda_result.down_peak.peak_bin,
         "FFTW and CUDA select the same non-interpolated peak bins");
  expectNear(cuda_result.up_peak.magnitude_db, fftw_result.up_peak.magnitude_db, 0.05,
             "FFTW and CUDA UP peak magnitude agrees");
  expectNear(cuda_result.down_peak.magnitude_db, fftw_result.down_peak.magnitude_db, 0.05,
             "FFTW and CUDA DOWN peak magnitude agrees");
  expectNear(cuda_result.distance_m, fftw_result.distance_m, 1.0e-6,
             "FFTW and CUDA distance agrees");
  expectNear(cuda_result.velocity_mps, fftw_result.velocity_mps, 1.0e-6,
             "FFTW and CUDA velocity agrees");
  expectNear(cuda_result.point.x, fftw_result.point.x, 1.0e-6,
             "FFTW and CUDA X agrees");
  expectNear(cuda_result.point.y, fftw_result.point.y, 1.0e-6,
             "FFTW and CUDA Y agrees");
  expectNear(cuda_result.point.z, fftw_result.point.z, 1.0e-6,
             "FFTW and CUDA Z agrees");
  expectNear(cuda_result.point.intensity, fftw_result.point.intensity, 0.05,
             "FFTW and CUDA intensity agrees");
  expectNear(cuda_result.point.velocity, fftw_result.point.velocity, 1.0e-6,
             "FFTW and CUDA point velocity agrees");

  fmcw::RawFrame silent = raw;
  std::fill(silent.samples.begin(), silent.samples.end(), 0);
  fmcw::ProcessedFrame fftw_invalid;
  fmcw::ProcessedFrame cuda_invalid;
  expect(fftw_processor.process(silent, fftw_invalid, error) &&
             cuda_processor.process(silent, cuda_invalid, error),
         "FFTW and CUDA process the same below-threshold input");
  expect(!fftw_invalid.measurement_valid && !cuda_invalid.measurement_valid &&
             std::isnan(fftw_invalid.distance_m) && std::isnan(cuda_invalid.distance_m) &&
             std::isnan(fftw_invalid.up_peak.peak_bin) && std::isnan(cuda_invalid.up_peak.peak_bin),
         "FFTW and CUDA preserve the same below-threshold NaN contract");
}

void expectBatchParity(const fmcw::ProcessedFrame& batch,
                       const fmcw::ProcessedFrame& reference,
                       const std::string& record_name,
                       double magnitude_tolerance = 1.0e-4) {
  expect(batch.up_peak.valid == reference.up_peak.valid &&
             batch.down_peak.valid == reference.down_peak.valid &&
             batch.measurement_valid == reference.measurement_valid &&
             batch.point.valid == reference.point.valid,
         record_name + " preserves peak, measurement, and point validity");
  expect(batch.up_peak.discrete_bin == reference.up_peak.discrete_bin &&
             batch.down_peak.discrete_bin == reference.down_peak.discrete_bin,
         record_name + " preserves integer UP/DOWN peak bins");
  expect(!batch.processing_note.empty(), record_name + " records its batch processing backend");
  if (reference.measurement_valid) {
    expectNear(batch.up_peak.magnitude_db, reference.up_peak.magnitude_db, magnitude_tolerance,
               record_name + " preserves UP dBFS magnitude");
    expectNear(batch.down_peak.magnitude_db, reference.down_peak.magnitude_db, magnitude_tolerance,
               record_name + " preserves DOWN dBFS magnitude");
    expectNear(batch.distance_m, reference.distance_m, 1.0e-6,
               record_name + " preserves distance");
    expectNear(batch.velocity_mps, reference.velocity_mps, 1.0e-6,
               record_name + " preserves velocity");
    expectNear(batch.point.x, reference.point.x, 1.0e-6, record_name + " preserves X");
    expectNear(batch.point.y, reference.point.y, 1.0e-6, record_name + " preserves Y");
    expectNear(batch.point.z, reference.point.z, 1.0e-6, record_name + " preserves Z");
    expectNear(batch.point.intensity, reference.point.intensity, magnitude_tolerance,
               record_name + " preserves intensity");
    expectNear(batch.point.velocity, reference.point.velocity, 1.0e-6,
               record_name + " preserves point velocity");
  } else {
    expect(std::isnan(batch.up_peak.peak_bin) && std::isnan(batch.down_peak.peak_bin) &&
               std::isnan(batch.up_peak.magnitude_db) && std::isnan(batch.down_peak.magnitude_db) &&
               std::isnan(batch.distance_m) && std::isnan(batch.velocity_mps) &&
               std::isnan(batch.point.x) && std::isnan(batch.point.y) &&
               std::isnan(batch.point.z) && std::isnan(batch.point.intensity) &&
               std::isnan(batch.point.velocity),
           record_name + " preserves the below-threshold NaN contract");
  }
}

void testCudaBatchParity(const fmcw::SystemConfig& base_config,
                         const std::vector<fmcw::RawFramePtr>& frames) {
  if (!fmcw::CudaFftBackend::available()) {
    std::cout << "CUDA full-batch parity skipped: no runtime CUDA device.\n";
    return;
  }
  constexpr std::uint32_t selected_record_index = 2U;
  fmcw::RawFrameBatch raw_batch;
  for (std::size_t index = 0; index < frames.size(); ++index) {
    raw_batch.records.push_back(*frames[index]);
    raw_batch.records.back().metadata.record_index_in_buffer = static_cast<std::uint32_t>(index);
    raw_batch.records.back().metadata.records_in_buffer = static_cast<std::uint32_t>(frames.size());
  }
  std::fill(raw_batch.records.back().samples.begin(), raw_batch.records.back().samples.end(), 0);

  auto fftw_config = base_config;
  fftw_config.processing.fft_backend = fmcw::FftBackendKind::Fftw;
  auto cuda_config = base_config;
  cuda_config.processing.fft_backend = fmcw::FftBackendKind::Cuda;
  fmcw::SignalProcessor fftw_processor(std::make_unique<fmcw::FftwBackend>());
  fmcw::SignalProcessor cuda_processor(std::make_unique<fmcw::CudaFftBackend>());
  std::string error;
  const bool configured = fftw_processor.configure(fftw_config, 13U, error) &&
      cuda_processor.configure(cuda_config, 13U, error);
  expect(configured, "FFTW and CUDA full-batch processors configure identically");
  if (!configured) {
    return;
  }

  std::vector<fmcw::ProcessedFrame> fftw_results;
  std::vector<fmcw::ProcessedFrame> cuda_results;
  const bool processed = fftw_processor.processBatch(raw_batch, selected_record_index,
                                                     fftw_results, error) &&
      cuda_processor.processBatch(raw_batch, selected_record_index, cuda_results, error);
  expect(processed && fftw_results.size() == raw_batch.records.size() &&
             cuda_results.size() == raw_batch.records.size(),
         "FFTW and CUDA full pipelines return every batch record");
  if (!processed || fftw_results.size() != cuda_results.size()) {
    return;
  }

  for (std::size_t index = 0; index < cuda_results.size(); ++index) {
    expectBatchParity(cuda_results[index], fftw_results[index],
                      "CUDA batch record " + std::to_string(index), 0.05);
    const bool selected = index == selected_record_index;
    expect((!cuda_results[index].up_fft_magnitude_db.empty()) == selected &&
               (!cuda_results[index].down_fft_magnitude_db.empty()) == selected,
           "CUDA copies spectra only for the selected record");
  }
  const auto& fftw_selected = fftw_results[selected_record_index];
  const auto& cuda_selected = cuda_results[selected_record_index];
  expect(cuda_selected.up_fft_magnitude_db.size() == fftw_selected.up_fft_magnitude_db.size() &&
             cuda_selected.down_fft_magnitude_db.size() == fftw_selected.down_fft_magnitude_db.size(),
         "CUDA selected spectra have the FFTW reference shape");
  if (cuda_selected.up_fft_magnitude_db.size() == fftw_selected.up_fft_magnitude_db.size() &&
      cuda_selected.down_fft_magnitude_db.size() == fftw_selected.down_fft_magnitude_db.size()) {
    double maximum_signal_difference_db = 0.0;
    double maximum_noise_difference_db = 0.0;
    for (std::size_t index = 0; index < cuda_selected.up_fft_magnitude_db.size(); ++index) {
      const auto collect_difference = [&](double cuda_value, double fftw_value) {
        const auto difference = std::abs(cuda_value - fftw_value);
        if (std::max(cuda_value, fftw_value) > -100.0) {
          maximum_signal_difference_db = std::max(maximum_signal_difference_db, difference);
        } else {
          maximum_noise_difference_db = std::max(maximum_noise_difference_db, difference);
        }
      };
      collect_difference(cuda_selected.up_fft_magnitude_db[index],
                         fftw_selected.up_fft_magnitude_db[index]);
      collect_difference(cuda_selected.down_fft_magnitude_db[index],
                         fftw_selected.down_fft_magnitude_db[index]);
    }
    expect(maximum_signal_difference_db <= 0.05,
           "CUDA selected signal spectrum agrees with FFTW within 0.05 dB");
    expect(maximum_noise_difference_db <= 2.0,
           "CUDA selected numerical noise floor agrees with FFTW within 2 dB");
  }

  auto native_batch = raw_batch;
  for (auto& record : native_batch.records) {
    for (auto& sample : record.samples) {
      const auto offset_binary = static_cast<std::uint16_t>(
          std::clamp(static_cast<std::int32_t>(sample) + 32768, 0, 65535)) & 0xFFF0U;
      sample = static_cast<std::int16_t>(offset_binary);
    }
    record.metadata.sample_format = fmcw::SampleFormat::UnsignedOffsetBinary12LeftAligned;
  }
  std::vector<fmcw::ProcessedFrame> native_fftw_results;
  std::vector<fmcw::ProcessedFrame> native_cuda_results;
  const bool native_processed = fftw_processor.processBatch(
      native_batch, selected_record_index, native_fftw_results, error) &&
      cuda_processor.processBatch(native_batch, selected_record_index,
                                  native_cuda_results, error);
  expect(native_processed && native_fftw_results.size() == native_cuda_results.size(),
         "FFTW and CUDA accept the native ATS9371 DMA sample format");
  if (native_processed && native_fftw_results.size() == native_cuda_results.size()) {
    for (std::size_t index = 0; index < native_cuda_results.size(); ++index) {
      expectBatchParity(native_cuda_results[index], native_fftw_results[index],
                        "Native ATS CUDA batch record " + std::to_string(index), 0.05);
    }
  }

  const auto record_count = raw_batch.records.size();
  const auto record_length = raw_batch.records.front().samples.size();
  auto external_samples = std::make_shared<std::vector<std::int16_t>>(
      record_count * record_length);
  auto external_batch = std::make_shared<fmcw::RawFrameBatch>();
  external_batch->metadata = raw_batch.metadata;
  external_batch->metadata.record_count = static_cast<std::uint32_t>(record_count);
  external_batch->metadata.record_length = static_cast<std::uint32_t>(record_length);
  external_batch->contiguous_samples.setView(external_samples->data(),
                                             external_samples->size());
  external_batch->records.resize(record_count);
  for (std::size_t index = 0; index < record_count; ++index) {
    auto* destination = external_samples->data() + index * record_length;
    std::copy(native_batch.records[index].samples.begin(), native_batch.records[index].samples.end(),
              destination);
    external_batch->records[index].metadata = native_batch.records[index].metadata;
    external_batch->records[index].samples.setView(destination, record_length);
  }
  external_batch->sample_owner = external_samples;
  std::weak_ptr<std::vector<std::int16_t>> dma_lease = external_samples;
  external_samples.reset();

  fmcw::RawFrameBatchPtr submitted_batch = external_batch;
  external_batch.reset();
  const bool submitted = cuda_processor.submitBatch(
      std::move(submitted_batch), selected_record_index, error);
  expect(submitted && cuda_processor.inFlightBatchCount() == 1U && !dma_lease.expired(),
         "CUDA slot 1 retains the external DMA input until H2D completes");
  if (submitted) {
    expect(cuda_processor.releaseCompletedBatchInputs(true, error) && dma_lease.expired() &&
               cuda_processor.inFlightBatchCount() == 1U,
           "CUDA H2D event releases the DMA lease before full processing completes");
    fmcw::RawFrameBatchPtr completed_batch;
    std::vector<fmcw::ProcessedFrame> completed_results;
    bool collected = false;
    expect(cuda_processor.collectNextBatch(true, completed_batch, completed_results,
                                           collected, error) &&
               collected && completed_batch &&
               completed_results.size() == record_count &&
               cuda_processor.inFlightBatchCount() == 0U,
           "CUDA completion event collects every result from the single slot");
  }
}

void testFftwBatchParity(const fmcw::SystemConfig& config,
                         const std::vector<fmcw::RawFramePtr>& frames) {
  constexpr std::uint32_t selected_record_index = 2U;
  fmcw::RawFrameBatch raw_batch;
  for (std::size_t index = 0; index < frames.size(); ++index) {
    raw_batch.records.push_back(*frames[index]);
    raw_batch.records.back().metadata.record_index_in_buffer = static_cast<std::uint32_t>(index);
    raw_batch.records.back().metadata.records_in_buffer = static_cast<std::uint32_t>(frames.size());
  }
  std::fill(raw_batch.records.back().samples.begin(), raw_batch.records.back().samples.end(), 0);

  fmcw::SignalProcessor reference_processor(std::make_unique<fmcw::FftwBackend>());
  fmcw::SignalProcessor batch_processor(std::make_unique<fmcw::FftwBackend>());
  std::string error;
  expect(reference_processor.configure(config, 11U, error) &&
             batch_processor.configure(config, 11U, error),
         "single-record and batch FFTW processors configure identically");
  std::vector<fmcw::ProcessedFrame> reference_results(raw_batch.records.size());
  bool references_ready = true;
  for (std::size_t index = 0; index < raw_batch.records.size(); ++index) {
    references_ready = reference_processor.process(raw_batch.records[index],
                                                   reference_results[index], error) &&
        references_ready;
  }
  std::vector<fmcw::ProcessedFrame> batch_results;
  const bool batch_ready = batch_processor.processBatch(raw_batch, selected_record_index,
                                                        batch_results, error);
  expect(references_ready && batch_ready && batch_results.size() == reference_results.size(),
         "FFTW plan-many batch returns one result per input record");
  if (!references_ready || !batch_ready || batch_results.size() != reference_results.size()) {
    return;
  }

  for (std::size_t index = 0; index < batch_results.size(); ++index) {
    expectBatchParity(batch_results[index], reference_results[index],
                      "batch record " + std::to_string(index));
    const bool selected = index == selected_record_index;
    expect((!batch_results[index].up_fft_magnitude_db.empty()) == selected &&
               (!batch_results[index].down_fft_magnitude_db.empty()) == selected,
           "only the selected record retains UI FFT spectra");
  }
  const auto& selected_batch = batch_results[selected_record_index];
  const auto& selected_reference = reference_results[selected_record_index];
  expect(selected_batch.up_fft_magnitude_db.size() == selected_reference.up_fft_magnitude_db.size() &&
             selected_batch.down_fft_magnitude_db.size() == selected_reference.down_fft_magnitude_db.size(),
         "selected record retains complete UP/DOWN spectra");
  if (selected_batch.up_fft_magnitude_db.size() == selected_reference.up_fft_magnitude_db.size() &&
      selected_batch.down_fft_magnitude_db.size() == selected_reference.down_fft_magnitude_db.size()) {
    for (std::size_t index = 0; index < selected_batch.up_fft_magnitude_db.size(); ++index) {
      expectNear(selected_batch.up_fft_magnitude_db[index],
                 selected_reference.up_fft_magnitude_db[index], 1.0e-4,
                 "selected UP spectrum matches single-record FFTW");
      expectNear(selected_batch.down_fft_magnitude_db[index],
                 selected_reference.down_fft_magnitude_db[index], 1.0e-4,
                 "selected DOWN spectrum matches single-record FFTW");
    }
  }
}

struct CountingBatchFftState {
  fmcw::FftPlan plan;
  std::size_t prepare_calls = 0U;
  std::size_t execute_calls = 0U;
  std::size_t transforms_executed = 0U;
};

class CountingBatchFftBackend final : public fmcw::IFftBackend {
 public:
  explicit CountingBatchFftBackend(std::shared_ptr<CountingBatchFftState> state)
      : state_(std::move(state)) {}

  std::string name() const override { return "Counting FFTW batch backend"; }
  fmcw::FftBackendKind kind() const override { return fmcw::FftBackendKind::Fftw; }

  bool prepare(const fmcw::FftPlan& plan, std::string& error) override {
    state_->plan = plan;
    ++state_->prepare_calls;
    error.clear();
    return plan.length > 1U && plan.batch > 0U;
  }

  bool execute(const std::vector<float>& input,
               std::vector<std::complex<float>>& output,
               std::string& error) override {
    if (input.size() != state_->plan.length * state_->plan.batch) {
      error = "Counting FFT batch input does not match its plan";
      return false;
    }
    ++state_->execute_calls;
    state_->transforms_executed += state_->plan.batch;
    const auto spectrum_length = state_->plan.length / 2U + 1U;
    output.assign(spectrum_length * state_->plan.batch, {});
    for (std::size_t transform = 0; transform < state_->plan.batch; ++transform) {
      const std::size_t peak_bin = transform % 2U == 0U ? 37U : 43U;
      output[transform * spectrum_length + peak_bin] = {4096.0F, 0.0F};
    }
    error.clear();
    return true;
  }

 private:
  std::shared_ptr<CountingBatchFftState> state_;
};

void testQualificationBatchExecutionCount() {
  auto config = fmcw::makeAts9371QualificationSimulatorConfig();
  auto state = std::make_shared<CountingBatchFftState>();
  fmcw::SignalProcessor processor(std::make_unique<CountingBatchFftBackend>(state));
  std::string error;
  expect(processor.configure(config, 12U, error),
         "998-record qualification batch processor configures");

  fmcw::RawFrame template_frame;
  template_frame.samples.assign(config.digitizer.sample_point, 0);
  template_frame.metadata.frame_kind = fmcw::FrameKind::FullChirpPeriod;
  template_frame.metadata.sample_rate_hz = config.digitizer.sample_rate_hz;
  template_frame.metadata.record_length = config.digitizer.sample_point;
  template_frame.metadata.up_segment = config.chirp_segmentation.up_segment;
  template_frame.metadata.down_segment = config.chirp_segmentation.down_segment;
  template_frame.metadata.records_in_buffer = config.digitizer.records_per_buffer;
  template_frame.metadata.scan_position.valid = true;

  fmcw::RawFrameBatch raw_batch;
  raw_batch.records.resize(config.digitizer.records_per_buffer, template_frame);
  for (std::size_t index = 0; index < raw_batch.records.size(); ++index) {
    auto& record = raw_batch.records[index];
    record.metadata.frame_id = index + 1U;
    record.metadata.record_index_in_buffer = static_cast<std::uint32_t>(index);
    record.metadata.scan_position.x_index = static_cast<std::uint32_t>(index);
  }
  raw_batch.metadata.record_count = config.digitizer.records_per_buffer;
  raw_batch.metadata.record_length = config.digitizer.sample_point;

  std::vector<fmcw::ProcessedFrame> results;
  expect(processor.processBatch(raw_batch, 997U, results, error),
         "998-record qualification workload executes as FFTW batches");
  expect(results.size() == 998U && results.front().measurement_valid &&
             results.back().measurement_valid,
         "qualification workload produces all 998 measurement results");
  expect(state->execute_calls == 16U && state->transforms_executed == 2048U,
         "998 records use 16 fixed FFT batches instead of 1,996 synchronous FFT calls");
  expect(results.front().up_fft_magnitude_db.empty() &&
             !results.back().up_fft_magnitude_db.empty(),
         "qualification workload retains spectra only for selected record 997");
}

fmcw::ProcessedFrame testSignalProcessing(const fmcw::SystemConfig& config,
                                          const std::vector<fmcw::RawFramePtr>& frames) {
  fmcw::SignalProcessor processor(std::make_unique<fmcw::FftwBackend>());
  std::string error;
  expect(processor.configure(config, 1, error), "signal processor configures with FFTW");
  fmcw::ProcessedFrame processed;
  expect(processor.process(*frames.front(), processed, error), "full-period frame processes successfully");
  const double up_expected = 37.0 * config.chirp_segmentation.segment_fft_length /
      config.chirp_segmentation.up_segment.length();
  const double down_expected = 43.0 * config.chirp_segmentation.segment_fft_length /
      config.chirp_segmentation.down_segment.length();
  expectNear(processed.up_peak.peak_bin, up_expected, 0.75, "up chirp peak is detected");
  expectNear(processed.down_peak.peak_bin, down_expected, 0.75, "down chirp peak is detected");
  expect(processed.up_peak.peak_bin == static_cast<float>(processed.up_peak.discrete_bin) &&
             processed.down_peak.peak_bin == static_cast<float>(processed.down_peak.discrete_bin),
         "peak detection uses the maximum discrete bin without interpolation");
  expect(processed.measurement_valid && processed.point.valid, "valid paired peaks produce distance and XYZ");
  expect(processed.distance_m > 0.0F, "paired peaks produce positive distance");
  expect(processed.velocity_mps < 0.0F, "up/down peak difference preserves velocity sign");
  const double bin_frequency_hz = config.digitizer.sample_rate_hz /
      static_cast<double>(config.chirp_segmentation.segment_fft_length);
  const double expected_distance_m = 299792458.0 *
      (processed.up_peak.peak_bin + processed.down_peak.peak_bin) *
      bin_frequency_hz /
      (8.0 * config.laser.sweep_bandwidth_hz * config.laser.sweep_rate_hz) *
      config.calibration.distance_scale + config.calibration.distance_offset_m;
  expectNear(processed.distance_m, expected_distance_m, 1.0e-4,
             "distance uses bandwidth and sweep rate in Hz");
  const double expected_velocity_mps = config.calibration.velocity_wavelength_nm * 1.0e-9 *
      (processed.up_peak.peak_bin - processed.down_peak.peak_bin) * bin_frequency_hz / 4.0 *
      config.calibration.velocity_scale + config.calibration.velocity_offset_mps;
  expectNear(processed.velocity_mps, expected_velocity_mps, 1.0e-6,
             "velocity uses wavelength and the UP/DOWN beat-frequency difference");
  const double x_angle = (processed.scan_position.x_angle_deg + config.calibration.x_angle_offset_deg) *
      3.14159265358979323846 / 180.0;
  const double y_angle = (processed.scan_position.y_angle_deg + config.calibration.y_angle_offset_deg) *
      3.14159265358979323846 / 180.0;
  expectNear(processed.point.x, processed.distance_m * std::cos(y_angle) * std::sin(x_angle), 1.0e-5,
             "Cartesian X matches the legacy lateral-axis conversion");
  expectNear(processed.point.y, processed.distance_m * std::cos(y_angle) * std::cos(x_angle), 1.0e-5,
             "Cartesian Y matches the legacy forward-axis conversion");
  expectNear(processed.point.z, -processed.distance_m * std::sin(y_angle), 1.0e-5,
             "Cartesian Z matches the legacy vertical-axis conversion");
  expectNear(processed.point.intensity,
             0.5 * (processed.up_peak.magnitude_db + processed.down_peak.magnitude_db), 1.0e-6,
             "point intensity is the mean UP/DOWN peak magnitude");
  expectNear(processed.point.velocity, processed.velocity_mps, 1.0e-6,
             "point velocity carries the calibrated radial velocity");

  auto equality_config = config.processing;
  equality_config.peak_threshold_db = std::max(processed.up_peak.magnitude_db,
                                               processed.down_peak.magnitude_db);
  expect(processor.updateRuntimeConfig(equality_config, 2, error),
         "peak threshold accepts the detected peak magnitude for equality testing");
  fmcw::ProcessedFrame equality_rejected;
  expect(processor.process(*frames.front(), equality_rejected, error),
         "threshold equality frame is processed without crashing");
  expect(!equality_rejected.measurement_valid && !equality_rejected.up_peak.valid &&
             !equality_rejected.down_peak.valid,
         "a peak equal to the threshold is rejected because detection requires strict exceedance");

  auto runtime_config = config.processing;
  runtime_config.peak_threshold_db = -55.0;
  expect(processor.updateRuntimeConfig(runtime_config, 3, error), "peak threshold updates at a frame boundary");
  fmcw::RawFrame silent = *frames.at(1);
  std::fill(silent.samples.begin(), silent.samples.end(), 0);
  fmcw::ProcessedFrame invalid;
  expect(processor.process(silent, invalid, error), "silent frame is processed without crashing");
  expect(!invalid.measurement_valid && !invalid.up_peak.valid && !invalid.down_peak.valid &&
             invalid.up_peak.state == fmcw::PeakTrackState::Invalid &&
             invalid.down_peak.state == fmcw::PeakTrackState::Invalid,
         "each silent A-scan is independently marked invalid without carrying a previous peak");
  expect(invalid.up_peak.discrete_bin == -1 && invalid.down_peak.discrete_bin == -1 &&
             std::isnan(invalid.up_peak.peak_bin) &&
             std::isnan(invalid.down_peak.peak_bin) &&
             std::isnan(invalid.up_peak.magnitude_db) &&
             std::isnan(invalid.down_peak.magnitude_db),
         "threshold-rejected peaks expose NaN instead of numeric peak values");
  expect(std::isnan(invalid.distance_m) && std::isnan(invalid.velocity_mps) &&
             std::isnan(invalid.point.x) && std::isnan(invalid.point.y) &&
             std::isnan(invalid.point.z) && std::isnan(invalid.point.intensity) &&
             std::isnan(invalid.point.velocity),
         "threshold-rejected measurements expose NaN for distance, velocity, and XYZ");
  expect(invalid.processing_config_revision == 3, "processed frame records the applied runtime revision");
  return processed;
}

void testProcessingServiceSnapshots(const fmcw::SystemConfig& config,
                                    const std::vector<fmcw::RawFramePtr>& frames) {
  fmcw::ProcessingService service(std::make_unique<fmcw::FftwBackend>());
  std::atomic<std::uint64_t> callback_count{0};
  service.setProcessedFrameCallback([&callback_count](fmcw::ProcessedFramePtr) { ++callback_count; });
  std::string error;
  expect(service.configure(config, 3, error), "processing service configures");
  expect(service.start(error), "processing worker starts");
  auto runtime = config.processing;
  runtime.peak_threshold_db = -55.0;
  expect(service.updateRuntimeConfig(runtime, 4, error), "processing service accepts runtime peak settings");
  for (const auto& frame : frames) {
    expect(service.enqueue(frame, error) == fmcw::ProcessingEnqueueResult::Accepted,
           "acquisition-side enqueue remains non-blocking");
  }
  service.requestStop("Phase 4 test complete");
  expect(service.waitUntilStopped(error), "processing worker drains and stops");
  const auto status = service.status();
  expect(status.frames_processed == frames.size() && status.processing_config_revision == 4,
         "processing worker reports processed frames and active revision");
  expect(callback_count.load() == frames.size(), "processed callback receives each completed frame");
  const auto waveform = service.snapshots().latestWaveform();
  const auto fft = service.snapshots().latestFft();
  const auto line = service.snapshots().latestScanLine();
  const auto bscan = service.snapshots().latestBScan();
  expect(waveform && waveform->full_scale_samples.size() == config.digitizer.sample_point,
         "waveform snapshot owns a UI-safe sample copy");
  expect(fft && !fft->up_magnitude_db.empty(), "FFT snapshot is published");
  expect(line && line->distance_m.size() == config.scan.x_pixel_count,
         "completed scan line publishes peak and distance arrays");
  expect(bscan && bscan->width == config.scan.x_pixel_count && bscan->height == config.scan.y_line_count &&
             bscan->completed_lines == 1,
         "completed line updates the X by B-scan forward-depth matrix");
}

void testProcessingServiceBatch(const fmcw::SystemConfig& config,
                                const std::vector<fmcw::RawFramePtr>& frames) {
  fmcw::ProcessingService service(std::make_unique<fmcw::FftwBackend>());
  std::string error;
  expect(service.configure(config, 5, error) && service.start(error),
         "batch processing service configures and starts");
  auto mutable_batch = std::make_shared<fmcw::RawFrameBatch>();
  for (const auto& frame : frames) {
    mutable_batch->records.push_back(*frame);
  }
  mutable_batch->metadata.sequence = 12;
  mutable_batch->metadata.record_count = static_cast<std::uint32_t>(mutable_batch->records.size());
  mutable_batch->metadata.record_length = config.digitizer.sample_point;
  const auto timestamp_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
  mutable_batch->metadata.completion_timestamp_ns = timestamp_ns - 10'000'000U;
  mutable_batch->metadata.acquisition_wakeup_timestamp_ns = timestamp_ns - 9'000'000U;
  mutable_batch->metadata.ownership_ready_timestamp_ns = timestamp_ns - 8'000'000U;
  mutable_batch->metadata.session_ready_timestamp_ns = timestamp_ns - 7'000'000U;
  fmcw::RawFrameBatchPtr batch = mutable_batch;
  expect(service.enqueueBatch(std::move(batch), error) == fmcw::ProcessingEnqueueResult::Accepted,
         "one immutable DMA batch enters the processing queue");
  service.requestStop("Phase 7.2 batch test complete");
  expect(service.waitUntilStopped(error), "batch processing service drains and stops");
  const auto status = service.status();
  const auto stage_total_ms = status.last_latency.acquisition_wakeup_ms +
      status.last_latency.digitizer_materialization_ms +
      status.last_latency.session_validation_ms +
      status.last_latency.enqueue_dispatch_ms +
      status.last_latency.queue_wait_ms + status.last_latency.compute_ms;
  expect(status.batches_processed == 1U && status.frames_processed == frames.size(),
         "processing service accounts for one batch and every record in it");
  expect(status.last_ownership_copy_latency_ms >= 1.99 &&
             status.last_signal_processing_latency_ms >= 7.99 &&
             status.last_batch_latency_ms >= 10.0 &&
             status.last_latency.acquisition_wakeup_ms >= 0.99 &&
             status.last_latency.digitizer_materialization_ms >= 0.99 &&
             status.last_latency.session_validation_ms >= 0.99 &&
             status.last_latency.enqueue_dispatch_ms >= 6.99 &&
             std::abs(stage_total_ms - status.last_batch_latency_ms) < 1.0e-6 &&
             status.average_batch_latency_ms == status.last_batch_latency_ms &&
             status.maximum_ownership_copy_latency_ms ==
                 status.last_ownership_copy_latency_ms &&
             status.maximum_signal_processing_latency_ms ==
                 status.last_signal_processing_latency_ms &&
             status.maximum_batch_latency_ms == status.last_batch_latency_ms &&
             status.maximum_latency.compute_ms == status.last_latency.compute_ms &&
             status.batch_latency_p50_ms == status.batch_latency_p99_ms &&
             status.batch_deadline_misses == 1U,
         "processing telemetry reports each DMA-to-result stage, arithmetic mean, and deadline timing");
}

struct BlockingFftState {
  std::mutex mutex;
  std::condition_variable condition;
  bool execute_started = false;
  bool release = false;
};

class BlockingFftBackend final : public fmcw::IFftBackend {
 public:
  explicit BlockingFftBackend(std::shared_ptr<BlockingFftState> state) : state_(std::move(state)) {}

  std::string name() const override { return "Blocking FFT test backend"; }
  fmcw::FftBackendKind kind() const override { return fmcw::FftBackendKind::Fftw; }

  bool prepare(const fmcw::FftPlan& plan, std::string& error) override {
    length_ = plan.length;
    batch_ = plan.batch;
    error.clear();
    return length_ > 1U && batch_ > 0U;
  }

  bool execute(const std::vector<float>& input, std::vector<std::complex<float>>& output,
               std::string& error) override {
    {
      std::unique_lock<std::mutex> lock(state_->mutex);
      if (!state_->execute_started) {
        state_->execute_started = true;
        state_->condition.notify_all();
        state_->condition.wait(lock, [this] { return state_->release; });
      }
    }
    if (input.size() != length_ * batch_) {
      error = "Blocking FFT input length mismatch";
      return false;
    }
    output.assign((length_ / 2U + 1U) * batch_, {});
    error.clear();
    return true;
  }

 private:
  std::shared_ptr<BlockingFftState> state_;
  std::size_t length_ = 0U;
  std::size_t batch_ = 0U;
};

void testProcessingOverflow(fmcw::SystemConfig config, const std::vector<fmcw::RawFramePtr>& frames) {
  config.processing.queue_capacity = 1;
  auto state = std::make_shared<BlockingFftState>();
  fmcw::ProcessingService service(std::make_unique<BlockingFftBackend>(state));
  std::string error;
  expect(service.configure(config, 5, error) && service.start(error),
         "blocking processing service configures and starts");
  expect(service.enqueue(frames.at(0), error) == fmcw::ProcessingEnqueueResult::Accepted,
         "first frame reaches the blocking FFT backend");
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    expect(state->condition.wait_for(lock, std::chrono::seconds(2), [state] { return state->execute_started; }),
           "blocking FFT begins its first execution");
  }
  expect(service.enqueue(frames.at(1), error) == fmcw::ProcessingEnqueueResult::Accepted,
         "second frame occupies the bounded processing queue");
  expect(service.enqueue(frames.at(2), error) == fmcw::ProcessingEnqueueResult::Overflow,
         "third frame triggers processing stop-on-overflow");
  expect(service.status().stop_requested, "processing overflow is exposed as a stop request");
  std::string stop_reason;
  expect(service.stopRequested(stop_reason) &&
             stop_reason == "Processing queue capacity exceeded",
         "lightweight processing stop check preserves the overflow reason");
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->release = true;
    state->condition.notify_all();
  }
  expect(service.waitUntilStopped(error), "overflowed processing service drains accepted frames and stops");
}

void testProcessingOperatorStopDiscardsBacklog(fmcw::SystemConfig config,
                                               const std::vector<fmcw::RawFramePtr>& frames) {
  config.processing.queue_capacity = 4;
  auto state = std::make_shared<BlockingFftState>();
  fmcw::ProcessingService service(std::make_unique<BlockingFftBackend>(state));
  std::string error;
  expect(service.configure(config, 6, error) && service.start(error),
         "operator-stop processing service starts");
  expect(service.enqueue(frames.at(0), error) == fmcw::ProcessingEnqueueResult::Accepted,
         "operator-stop test starts one processing batch");
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    expect(state->condition.wait_for(lock, std::chrono::seconds(2), [state] { return state->execute_started; }),
           "operator-stop test enters the active FFT batch");
  }
  expect(service.enqueue(frames.at(1), error) == fmcw::ProcessingEnqueueResult::Accepted &&
             service.enqueue(frames.front(), error) == fmcw::ProcessingEnqueueResult::Accepted,
         "operator-stop test queues two pending batches");
  service.requestStop("Stopped by operator", fmcw::ProcessingStopMode::DiscardPending);
  const auto stopping_status = service.status();
  expect(stopping_status.queue_size == 0U &&
             stopping_status.batches_discarded_on_stop == 2U,
         "operator Stop discards processing batches that have not started");
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->release = true;
    state->condition.notify_all();
  }
  expect(service.waitUntilStopped(error), "operator-stop processing worker joins");
  expect(service.status().frames_processed == 1U,
         "operator Stop finishes only the batch that was already executing");
}

std::filesystem::path uniqueTestDirectory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("fmcw_phase4_" + std::to_string(suffix));
}

void testBinaryStorageAndReplay(const fmcw::SystemConfig& config, fmcw::RawFramePtr raw,
                                fmcw::RawFramePtr second_raw,
                                const fmcw::ProcessedFrame& processed) {
  const auto directory = uniqueTestDirectory();
  fmcw::WriterOpenOptions options;
  options.session_directory = directory;
  options.file_stem = "session";
  options.session.session_id = "phase4-test";
  options.session.profile_id = config.profile.id;
  options.session.platform = "Windows";
  options.session.application_version = "test";
  options.session.start_timestamp_utc_ns = 1;
  options.session.config_snapshot_json = "{\"profile\":\"phase4-test\"}";
  options.raw_stream.channel = raw->metadata.channel;
  options.raw_stream.format_version = fmcw::kRawFrameFormatVersion;
  options.raw_stream.sample_format = raw->metadata.sample_format;
  options.raw_stream.byte_order = raw->metadata.byte_order;
  options.raw_stream.sample_rate_hz = raw->metadata.sample_rate_hz;
  options.raw_stream.record_length = raw->metadata.record_length;
  options.raw_enabled = true;
  options.processed_enabled = true;
  options.queue_capacity = 8;
  options.flush_interval_frames = 1;
  options.split_file_size_gb = 1.0e-6;

  fmcw::AsyncStorageService storage;
  std::string error;
  expect(storage.start(options, error), "asynchronous binary storage starts");
  expect(storage.enqueueRaw(raw, error) == fmcw::EnqueueResult::Accepted, "raw frame enters writer queue");
  expect(storage.enqueueRaw(second_raw, error) == fmcw::EnqueueResult::Accepted,
         "second raw frame enters the split writer queue");
  expect(storage.enqueueProcessed(std::make_shared<const fmcw::ProcessedFrame>(processed), error) ==
             fmcw::EnqueueResult::Accepted,
         "processed frame enters writer queue");
  storage.requestStop("unit-test stop");
  expect(storage.waitUntilStopped(error), "storage drains and finalizes both streams");
  const auto status = storage.status();
  expect(status.raw_writer.frames_written == 2 && status.processed_writer.frames_written == 1,
         "raw and processed writers report their accepted frames");

  const auto raw_path = directory / "session.raw.0000.bin";
  expect(std::filesystem::exists(raw_path) && std::filesystem::exists(directory / "session.raw.0001.bin") &&
             std::filesystem::exists(directory / "session.raw.json") &&
             std::filesystem::exists(directory / "session.processed.bin") &&
             std::filesystem::exists(directory / "session.processed.json"),
         "binary streams and JSON sidecars are created");
  fmcw::RawReplayReader replay;
  expect(replay.open(raw_path, error), "raw replay opens the stored stream");
  fmcw::RawFrame replayed;
  expect(replay.readNext(replayed, error) == fmcw::ReplayReadResult::FrameReady,
         "raw replay reads the stored frame");
  expect(replayed.samples == raw->samples && replayed.metadata.frame_id == raw->metadata.frame_id &&
             replayed.metadata.up_segment.start_sample == raw->metadata.up_segment.start_sample,
         "raw replay preserves samples and full-period metadata");
  fmcw::SignalProcessor replay_processor(std::make_unique<fmcw::FftwBackend>());
  fmcw::ProcessedFrame replay_processed;
  expect(replay_processor.configure(config, 7, error) && replay_processor.process(replayed, replay_processed, error),
         "replayed raw frame uses the same signal processing pipeline");
  expect(replay_processed.up_peak.discrete_bin == processed.up_peak.discrete_bin &&
             replay_processed.down_peak.discrete_bin == processed.down_peak.discrete_bin,
         "replayed raw frame reproduces the detected peak bins");
  expect(replay.readNext(replayed, error) == fmcw::ReplayReadResult::FrameReady &&
             replayed.metadata.frame_id == second_raw->metadata.frame_id,
         "raw replay automatically advances to the next split part");
  expect(replay.readNext(replayed, error) == fmcw::ReplayReadResult::EndOfStream,
         "raw replay reports end of stream");
  replay.close();

  auto replay_config = config;
  replay_config.runtime.acquisition_source = fmcw::AcquisitionSource::Replay;
  replay_config.runtime.replay_file = raw_path.string();
  replay_config.runtime.replay_loop = false;
  fmcw::ReplayDigitizer replay_digitizer;
  expect(replay_digitizer.configure(replay_config, error) && replay_digitizer.connect(error) &&
             replay_digitizer.start(error),
         "runtime replay digitizer opens the stored raw stream");
  fmcw::MutableRawFrameBatchPtr replay_batch;
  expect(replay_digitizer.waitForBatch(replay_batch, std::chrono::milliseconds(100), error) ==
             fmcw::FrameWaitResult::FrameReady,
         "runtime replay digitizer publishes a DMA batch");
  expect(replay_batch && replay_batch->records.size() == 2U &&
             replay_batch->metadata.record_count == 2U &&
             replay_batch->records.front().samples == raw->samples &&
             replay_batch->records.back().samples == second_raw->samples,
         "runtime replay batch preserves both split-file records");
  replay_batch.reset();
  expect(replay_digitizer.waitForBatch(replay_batch, std::chrono::milliseconds(100), error) ==
             fmcw::FrameWaitResult::Stopped,
         "runtime replay digitizer reports finite stream completion");
  expect(replay_digitizer.stop(error), "runtime replay digitizer stops cleanly");
  replay_digitizer.disconnect();

  std::ifstream metadata(directory / "session.raw.json", std::ios::binary);
  std::ostringstream metadata_text;
  metadata_text << metadata.rdbuf();
  expect(metadata_text.str().find("unit-test stop") != std::string::npos &&
             metadata_text.str().find("config_snapshot") != std::string::npos,
         "raw JSON sidecar records stop reason and configuration snapshot");
  metadata.close();
  std::error_code remove_error;
  std::filesystem::remove_all(directory, remove_error);
  expect(!remove_error, "Phase 4 storage test directory is cleaned up");
}

struct BlockingWriterState {
  std::mutex mutex;
  std::condition_variable condition;
  bool write_started = false;
  bool release = false;
};

class BlockingRawWriter final : public fmcw::IRawFrameWriter {
 public:
  explicit BlockingRawWriter(std::shared_ptr<BlockingWriterState> state) : state_(std::move(state)) {}

  bool open(const fmcw::WriterOpenOptions&, std::string& error) override {
    status_.open = true;
    status_.recording = true;
    error.clear();
    return true;
  }

  bool write(const fmcw::RawFrame&, std::string& error) override {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->write_started = true;
    state_->condition.notify_all();
    state_->condition.wait(lock, [this] { return state_->release; });
    ++status_.frames_written;
    error.clear();
    return true;
  }

  bool flush(std::string& error) override {
    error.clear();
    return true;
  }

  bool finalize(const fmcw::WriterFinalizeOptions&, std::string& error) override {
    status_.open = false;
    status_.recording = false;
    error.clear();
    return true;
  }

  fmcw::WriterStatus status() const override { return status_; }

 private:
  std::shared_ptr<BlockingWriterState> state_;
  fmcw::WriterStatus status_;
};

void testBscanFrameBoundaryReset() {
  fmcw::ProcessingSnapshotStore snapshots;
  snapshots.configure(2, 2);
  fmcw::RawFrame raw;
  fmcw::ProcessedFrame processed;
  processed.measurement_valid = true;
  processed.point.y = 1.0F;
  processed.point.valid = true;

  const auto publish = [&](std::uint64_t frame_id, std::uint32_t x, std::uint32_t y) {
    raw.metadata.frame_id = frame_id;
    processed.frame_id = frame_id;
    processed.scan_position.x_index = x;
    processed.scan_position.y_index = y;
    processed.scan_position.valid = true;
    snapshots.publish(raw, processed);
  };

  publish(1, 0, 0);
  publish(2, 1, 0);
  expect(!snapshots.latestPointCloud(),
         "an incomplete raster does not publish a 3D point-cloud snapshot");
  publish(3, 0, 1);
  publish(4, 1, 1);
  auto bscan = snapshots.latestBScan();
  expect(bscan && bscan->completed_lines == 2 && bscan->scan_frame_index == 0 &&
             bscan->depth_m[3] == 1.0F,
         "first raster frame completes both B-scan lines with forward depth");
  const auto cloud = snapshots.latestPointCloud();
  expect(cloud && cloud->complete && cloud->points.size() == 4U && cloud->points[3].valid,
         "completed raster publishes an immutable 3D point-cloud snapshot");

  publish(5, 0, 0);
  publish(6, 1, 0);
  bscan = snapshots.latestBScan();
  expect(bscan && bscan->completed_lines == 1 && bscan->scan_frame_index == 1 && bscan->valid[2] == 0U,
         "new raster frame clears rows from the previous B-scan frame");
  const auto next_cloud = snapshots.latestPointCloud();
  expect(next_cloud == cloud && next_cloud->complete && next_cloud->scan_frame_index == 0 &&
             next_cloud->points[3].valid,
         "an incomplete next raster keeps the last complete 3D frame visible");

  publish(7, 0, 1);
  publish(8, 1, 1);
  const auto completed_next_cloud = snapshots.latestPointCloud();
  expect(completed_next_cloud && completed_next_cloud != cloud && completed_next_cloud->complete &&
             completed_next_cloud->scan_frame_index == 1 && completed_next_cloud->completed_lines == 2,
         "the 3D point cloud is replaced exactly when the next raster frame completes");
}

void testInvalidMeasurementSnapshots() {
  fmcw::ProcessingSnapshotStore snapshots;
  snapshots.configure(1, 1);
  fmcw::RawFrame raw;
  raw.metadata.frame_id = 1;
  raw.metadata.record_index_in_buffer = 0;
  raw.metadata.records_in_buffer = 1;
  fmcw::ProcessedFrame processed;
  processed.frame_id = 1;
  processed.scan_position.x_index = 0;
  processed.scan_position.y_index = 0;
  processed.scan_position.valid = true;
  snapshots.publish(raw, processed);

  const auto fft = snapshots.latestFft();
  const auto line = snapshots.latestScanLine();
  const auto bscan = snapshots.latestBScan();
  const auto cloud = snapshots.latestPointCloud();
  expect(fft && !fft->up_peak.valid && std::isnan(fft->up_peak.peak_bin) &&
             std::isnan(fft->up_peak.magnitude_db),
         "selected FFT snapshot preserves an invalid peak as NaN");
  expect(line && line->valid[0] == 0U && std::isnan(line->up_peak_index[0]) &&
             std::isnan(line->down_peak_index[0]) && std::isnan(line->up_peak_value_db[0]) &&
             std::isnan(line->down_peak_value_db[0]) && std::isnan(line->distance_m[0]) &&
             std::isnan(line->velocity_mps[0]) && std::isnan(line->depth_m[0]),
         "scan-line snapshot preserves threshold-rejected values as NaN");
  expect(bscan && bscan->valid[0] == 0U && std::isnan(bscan->depth_m[0]),
         "B-scan snapshot preserves an invalid measurement as NaN");
  expect(cloud && !cloud->points[0].valid && std::isnan(cloud->points[0].x) &&
             std::isnan(cloud->points[0].y) && std::isnan(cloud->points[0].z),
         "point-cloud snapshot preserves an invalid point as NaN");
}

void testSelectedAScanSnapshots() {
  fmcw::ProcessingSnapshotStore snapshots;
  snapshots.configure(2, 1);
  snapshots.setSelectedRecordIndex(1);
  fmcw::RawFrame raw;
  raw.samples = {100, -100};
  raw.metadata.records_in_buffer = 2;
  fmcw::ProcessedFrame processed;

  raw.metadata.frame_id = 1;
  raw.metadata.dma_buffer_sequence = 0;
  raw.metadata.record_index_in_buffer = 0;
  processed.frame_id = 1;
  snapshots.publish(raw, processed);
  expect(!snapshots.latestWaveform() && !snapshots.latestFft(),
         "non-selected A-scan does not replace Time Domain or FFT snapshots");

  raw.metadata.frame_id = 2;
  raw.metadata.record_index_in_buffer = 1;
  processed.frame_id = 2;
  snapshots.publish(raw, processed);
  const auto waveform = snapshots.latestWaveform();
  const auto fft = snapshots.latestFft();
  expect(waveform && fft && waveform->record_index_in_buffer == 1U &&
             waveform->dma_buffer_sequence == 0U && waveform->records_in_buffer == 2U,
         "selected A-scan publishes paired Time Domain and FFT snapshots");
}

void testStorageOverflow(fmcw::RawFramePtr raw) {
  auto state = std::make_shared<BlockingWriterState>();
  fmcw::AsyncStorageService storage(std::make_unique<BlockingRawWriter>(state),
                                    std::make_unique<fmcw::BinaryProcessedFrameWriter>());
  fmcw::WriterOpenOptions options;
  options.session_directory = uniqueTestDirectory();
  options.file_stem = "overflow";
  options.raw_enabled = true;
  options.processed_enabled = false;
  options.queue_capacity = 1;
  options.raw_stream.channel = raw->metadata.channel;
  options.raw_stream.format_version = fmcw::kRawFrameFormatVersion;
  options.raw_stream.sample_format = raw->metadata.sample_format;
  options.raw_stream.byte_order = raw->metadata.byte_order;
  options.raw_stream.sample_rate_hz = raw->metadata.sample_rate_hz;
  options.raw_stream.record_length = raw->metadata.record_length;
  std::string error;
  expect(storage.start(options, error), "blocking storage service starts");
  expect(storage.enqueueRaw(raw, error) == fmcw::EnqueueResult::Accepted, "first frame reaches blocking writer");
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    expect(state->condition.wait_for(lock, std::chrono::seconds(2), [state] { return state->write_started; }),
           "blocking writer begins its first write");
  }
  expect(storage.enqueueRaw(raw, error) == fmcw::EnqueueResult::Accepted,
         "second frame occupies the bounded queue");
  expect(storage.enqueueRaw(raw, error) == fmcw::EnqueueResult::Overflow,
         "third frame triggers stop-on-overflow");
  expect(storage.status().stop_requested, "overflow is exposed as a storage stop request");
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->release = true;
    state->condition.notify_all();
  }
  expect(storage.waitUntilStopped(error), "overflowed storage drains accepted work and stops");
  std::error_code remove_error;
  std::filesystem::remove_all(options.session_directory, remove_error);
}

}  // namespace

int main() {
  std::cout << "[Phase5] Selected A-scan snapshots\n" << std::flush;
  testSelectedAScanSnapshots();
  std::cout << "[Phase4] Invalid measurement NaN snapshots\n" << std::flush;
  testInvalidMeasurementSnapshots();
  if (!fmcw::FftwBackend::available()) {
    std::cout << "Phase 4 FFT tests skipped: FFTW3f was not found at configure time.\n";
    return failures == 0 ? 0 : 1;
  }
  std::cout << "[Phase4] FFT backends\n" << std::flush;
  testFftBackends();
  std::cout << "[Phase4] B-scan frame boundary\n" << std::flush;
  testBscanFrameBoundaryReset();
  const auto config = testConfig();
  std::cout << "[Phase4] Fake frames\n" << std::flush;
  const auto frames = makeFakeFrames(config, config.scan.x_pixel_count);
  if (frames.size() >= 2U) {
    std::cout << "[Phase4] FFTW/CUDA processing parity\n" << std::flush;
    testSignalProcessingBackendParity(config, *frames.front());
    std::cout << "[Phase7.3B] FFTW batch parity\n" << std::flush;
    testFftwBatchParity(config, frames);
    std::cout << "[Phase7.3C] CUDA full-batch parity\n" << std::flush;
    testCudaBatchParity(config, frames);
    std::cout << "[Phase7.3B] Qualification FFT batch execution count\n" << std::flush;
    testQualificationBatchExecutionCount();
    std::cout << "[Phase4] Signal processor\n" << std::flush;
    const auto processed = testSignalProcessing(config, frames);
    std::cout << "[Phase4] Processing service\n" << std::flush;
    testProcessingServiceSnapshots(config, frames);
    std::cout << "[Phase7.2] Processing DMA batch\n" << std::flush;
    testProcessingServiceBatch(config, frames);
    std::cout << "[Phase4] Processing overflow\n" << std::flush;
    testProcessingOverflow(config, frames);
    std::cout << "[Phase7.5] Operator Stop latency\n" << std::flush;
    testProcessingOperatorStopDiscardsBacklog(config, frames);
    std::cout << "[Phase4] Binary storage and replay\n" << std::flush;
    testBinaryStorageAndReplay(config, frames.front(), frames.at(1), processed);
    std::cout << "[Phase4] Storage overflow\n" << std::flush;
    testStorageOverflow(frames.front());
  }

  if (failures == 0) {
    std::cout << "All Phase 4 processing and storage tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
