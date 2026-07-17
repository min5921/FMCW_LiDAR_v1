#include "processing/signal_processor.h"

#include "core/config_validation.h"
#include "core/realtime_thread.h"
#include "processing/cuda/cuda_signal_pipeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#ifndef FMCW_HAS_OPENMP
#define FMCW_HAS_OPENMP 0
#endif

#if FMCW_HAS_OPENMP
#include <omp.h>
#endif

namespace fmcw {
namespace {

constexpr double kSpeedOfLightMps = 299792458.0;
constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kFftwBatchRecordChunk = 64U;
constexpr int kMaximumOpenMpBatchThreads = 16;

#if FMCW_HAS_OPENMP
int openMpBatchThreadCount(std::size_t work_item_count) {
  return std::max(1, std::min({static_cast<int>(work_item_count),
                               omp_get_max_threads(), kMaximumOpenMpBatchThreads}));
}
#endif

std::vector<float> makeWindow(WindowFunction function, std::size_t length) {
  std::vector<float> window(length, 1.0F);
  if (function == WindowFunction::Rectangular || length < 2U) {
    return window;
  }
  for (std::size_t index = 0; index < length; ++index) {
    const double phase = 2.0 * kPi * static_cast<double>(index) / static_cast<double>(length - 1U);
    switch (function) {
      case WindowFunction::Hann:
        window[index] = static_cast<float>(0.5 - 0.5 * std::cos(phase));
        break;
      case WindowFunction::Hamming:
        window[index] = static_cast<float>(0.54 - 0.46 * std::cos(phase));
        break;
      case WindowFunction::Blackman:
        window[index] = static_cast<float>(0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase));
        break;
      case WindowFunction::Rectangular:
        break;
    }
  }
  return window;
}

float windowSum(const std::vector<float>& window) {
  const double sum = std::accumulate(window.begin(), window.end(), 0.0);
  return static_cast<float>(std::max(sum, 1.0));
}

bool preprocessSegmentInto(const RawFrame& raw, SegmentRange range, const ProcessingConfig& processing,
                           SegmentPolarity polarity, bool down_segment, const std::vector<float>& window,
                           std::size_t fft_length, float* output, std::string& error) {
  if (!range.validFor(static_cast<std::uint32_t>(raw.samples.size())) || window.size() != range.length() ||
      fft_length < range.length() || output == nullptr) {
    error = "Segment preprocessing received an invalid range, window, or FFT length";
    return false;
  }
  if (fft_length > range.length()) {
    std::fill(output + range.length(), output + fft_length, 0.0F);
  }
  double mean = 0.0;
  if (processing.dc_removal) {
    for (std::uint32_t index = range.start_sample; index < range.end_sample_exclusive; ++index) {
      mean += sampleAsNormalizedFloat(raw.samples[index], raw.metadata.sample_format);
    }
    mean /= static_cast<double>(range.length());
  }
  for (std::size_t local = 0; local < range.length(); ++local) {
    float value = sampleAsNormalizedFloat(raw.samples[range.start_sample + local],
                                          raw.metadata.sample_format) -
        static_cast<float>(mean);
    if (down_segment && polarity == SegmentPolarity::InvertDown) {
      value = -value;
    }
    output[local] = value * window[local];
  }
  error.clear();
  return true;
}

bool preprocessSegment(const RawFrame& raw, SegmentRange range, const ProcessingConfig& processing,
                       SegmentPolarity polarity, bool down_segment, const std::vector<float>& window,
                       std::size_t fft_length, std::vector<float>& output, std::string& error) {
  output.resize(fft_length);
  return preprocessSegmentInto(raw, range, processing, polarity, down_segment, window,
                               fft_length, output.data(), error);
}

float magnitudeDbValue(const std::complex<float>& value, float coherent_sum) {
  const float scale = 2.0F / std::max(coherent_sum, 1.0F);
  const float amplitude = std::max(std::abs(value) * scale, 1.0e-10F);
  return 20.0F * std::log10(amplitude);
}

std::vector<float> magnitudeDb(const std::complex<float>* spectrum, std::size_t spectrum_size,
                               float coherent_sum) {
  std::vector<float> result(spectrum_size, -200.0F);
  for (std::size_t index = 0; index < spectrum_size; ++index) {
    result[index] = magnitudeDbValue(spectrum[index], coherent_sum);
  }
  return result;
}

std::vector<float> magnitudeDb(const std::vector<std::complex<float>>& spectrum, float coherent_sum) {
  return magnitudeDb(spectrum.data(), spectrum.size(), coherent_sum);
}

PeakMeasurement detectPeak(const std::complex<float>* spectrum, std::size_t spectrum_size,
                           float coherent_sum, std::uint32_t start_bin,
                           std::uint32_t end_bin, double threshold_db) {
  PeakMeasurement result;
  if (spectrum == nullptr || spectrum_size == 0U || start_bin >= spectrum_size) {
    return result;
  }
  const auto bounded_end = std::min<std::size_t>(end_bin, spectrum_size - 1U);
  if (start_bin > bounded_end) {
    return result;
  }
  std::size_t best_index = start_bin;
  float best_power = std::norm(spectrum[start_bin]);
  for (std::size_t index = start_bin + 1U; index <= bounded_end; ++index) {
    const auto current_power = std::norm(spectrum[index]);
    if (current_power > best_power) {
      best_power = current_power;
      best_index = index;
    }
  }
  const auto best_magnitude = magnitudeDbValue(spectrum[best_index], coherent_sum);
  if (!std::isfinite(best_magnitude) || best_magnitude <= static_cast<float>(threshold_db)) {
    return result;
  }
  result.discrete_bin = static_cast<std::int32_t>(best_index);
  result.peak_bin = static_cast<float>(best_index);
  result.magnitude_db = best_magnitude;
  result.state = PeakTrackState::Detected;
  result.valid = true;
  return result;
}

PeakMeasurement detectPeak(const std::vector<float>& magnitude, std::uint32_t start_bin,
                           std::uint32_t end_bin, double threshold_db) {
  PeakMeasurement result;
  if (magnitude.empty() || start_bin >= magnitude.size()) {
    return result;
  }
  const auto bounded_end = std::min<std::size_t>(end_bin, magnitude.size() - 1U);
  if (start_bin > bounded_end) {
    return result;
  }
  auto best = magnitude.begin() + static_cast<std::ptrdiff_t>(start_bin);
  for (std::size_t index = start_bin; index <= bounded_end; ++index) {
    if (magnitude[index] > *best) {
      best = magnitude.begin() + static_cast<std::ptrdiff_t>(index);
    }
  }
  const auto best_index = static_cast<std::size_t>(std::distance(magnitude.begin(), best));
  if (!std::isfinite(*best) || *best <= static_cast<float>(threshold_db)) {
    return result;
  }
  result.discrete_bin = static_cast<std::int32_t>(best_index);
  result.peak_bin = static_cast<float>(best_index);
  result.magnitude_db = *best;
  result.state = PeakTrackState::Detected;
  result.valid = true;
  return result;
}

PointXYZI toPoint(float distance_m, float velocity_mps, float intensity_db,
                  const ScanPosition& position, const CalibrationConfig& calibration) {
  PointXYZI point;
  if (!position.valid || !std::isfinite(distance_m) || !std::isfinite(velocity_mps) ||
      !std::isfinite(intensity_db) || !std::isfinite(position.x_angle_deg) ||
      !std::isfinite(position.y_angle_deg) || !std::isfinite(calibration.x_angle_offset_deg) ||
      !std::isfinite(calibration.y_angle_offset_deg)) {
    return point;
  }
  const double x_angle = (static_cast<double>(position.x_angle_deg) + calibration.x_angle_offset_deg) *
      kPi / 180.0;
  const double y_angle = (static_cast<double>(position.y_angle_deg) + calibration.y_angle_offset_deg) *
      kPi / 180.0;
  // Algebraic form of the legacy 90-degree angle transforms: X lateral, Y forward, Z vertical.
  const double horizontal_range = distance_m * std::cos(y_angle);
  point.x = static_cast<float>(horizontal_range * std::sin(x_angle));
  point.y = static_cast<float>(horizontal_range * std::cos(x_angle));
  point.z = static_cast<float>(-distance_m * std::sin(y_angle));
  point.intensity = intensity_db;
  point.velocity = velocity_mps;
  point.valid = true;
  return point;
}

bool rawMatchesConfig(const RawFrame& raw, const SystemConfig& config) {
  return raw.metadata.frame_kind == FrameKind::FullChirpPeriod &&
      raw.samples.size() == raw.metadata.record_length &&
      raw.metadata.up_segment.start_sample == config.chirp_segmentation.up_segment.start_sample &&
      raw.metadata.up_segment.end_sample_exclusive ==
          config.chirp_segmentation.up_segment.end_sample_exclusive &&
      raw.metadata.down_segment.start_sample == config.chirp_segmentation.down_segment.start_sample &&
      raw.metadata.down_segment.end_sample_exclusive ==
          config.chirp_segmentation.down_segment.end_sample_exclusive;
}

void initializeProcessed(const RawFrame& raw, std::uint64_t processing_config_revision,
                         ProcessedFrame& processed) {
  processed = {};
  processed.frame_id = raw.metadata.frame_id;
  processed.source_timestamp_ns = raw.metadata.host_timestamp_ns;
  processed.config_revision = raw.metadata.config_revision;
  processed.processing_config_revision = processing_config_revision;
  processed.scan_position = raw.metadata.scan_position;
}

void finalizeMeasurement(const RawFrame& raw, const SystemConfig& config,
                         ProcessedFrame& processed) {
  if (!processed.up_peak.valid || !processed.down_peak.valid) {
    return;
  }
  const double bin_frequency_hz = raw.metadata.sample_rate_hz /
      static_cast<double>(config.chirp_segmentation.segment_fft_length);
  const double up_frequency_hz = processed.up_peak.peak_bin * bin_frequency_hz;
  const double down_frequency_hz = processed.down_peak.peak_bin * bin_frequency_hz;
  const double wavelength_m = config.calibration.velocity_wavelength_nm * 1.0e-9;
  const double raw_distance = kSpeedOfLightMps * (up_frequency_hz + down_frequency_hz) /
      (8.0 * config.laser.sweep_bandwidth_hz * config.laser.sweep_rate_hz);
  const double raw_velocity = wavelength_m * (up_frequency_hz - down_frequency_hz) / 4.0;
  processed.distance_m = static_cast<float>(raw_distance * config.calibration.distance_scale +
      config.calibration.distance_offset_m);
  processed.velocity_mps = static_cast<float>(raw_velocity * config.calibration.velocity_scale +
      config.calibration.velocity_offset_mps);
  processed.measurement_valid = true;
  processed.point = toPoint(processed.distance_m, processed.velocity_mps,
                            0.5F * (processed.up_peak.magnitude_db + processed.down_peak.magnitude_db),
                            processed.scan_position, config.calibration);
}

}  // namespace

struct SignalProcessor::Impl {
  explicit Impl(std::unique_ptr<IFftBackend> backend) : fft_backend(std::move(backend)) {
    if (fft_backend != nullptr && fft_backend->kind() == FftBackendKind::Cuda) {
      cuda_pipeline = std::make_unique<CudaSignalPipeline>();
    }
  }

  std::unique_ptr<IFftBackend> fft_backend;
  std::unique_ptr<CudaSignalPipeline> cuda_pipeline;
  SystemConfig config;
  std::uint64_t processing_config_revision = 0;
  std::vector<float> up_window;
  std::vector<float> down_window;
  float up_window_sum = 1.0F;
  float down_window_sum = 1.0F;
  std::vector<float> batch_input;
  std::vector<std::complex<float>> batch_spectrum;
  std::size_t batch_record_capacity = 0;
  bool configured = false;
};

SignalProcessor::SignalProcessor(std::unique_ptr<IFftBackend> fft_backend)
    : impl_(std::make_unique<Impl>(std::move(fft_backend))) {}

SignalProcessor::~SignalProcessor() = default;

bool SignalProcessor::configure(const SystemConfig& config, std::uint64_t processing_config_revision,
                                std::string& error) {
  impl_->configured = false;
  if (impl_->fft_backend == nullptr) {
    error = "Signal processor requires an FFT backend";
    return false;
  }
  if (impl_->fft_backend->kind() != config.processing.fft_backend) {
    error = "Configured FFT backend does not match the selected processing profile";
    return false;
  }
  const auto validation = ConfigValidator::validate(config);
  if (validation.hasErrors()) {
    error = "Signal processor rejected invalid configuration";
    return false;
  }
  impl_->config = config;
  impl_->processing_config_revision = processing_config_revision;
  impl_->up_window = makeWindow(config.chirp_segmentation.window, config.chirp_segmentation.up_segment.length());
  impl_->down_window = makeWindow(config.chirp_segmentation.window, config.chirp_segmentation.down_segment.length());
  impl_->up_window_sum = windowSum(impl_->up_window);
  impl_->down_window_sum = windowSum(impl_->down_window);
  impl_->batch_record_capacity = impl_->fft_backend->kind() == FftBackendKind::Fftw
      ? std::min<std::size_t>(config.digitizer.records_per_buffer, kFftwBatchRecordChunk)
      : static_cast<std::size_t>(config.digitizer.records_per_buffer);
  const auto transform_count = impl_->batch_record_capacity * 2U;
  if (impl_->fft_backend->kind() == FftBackendKind::Cuda) {
    if (impl_->cuda_pipeline == nullptr ||
        !impl_->cuda_pipeline->configure(config, processing_config_revision,
                                         impl_->up_window, impl_->down_window,
                                         impl_->up_window_sum, impl_->down_window_sum, error)) {
      return false;
    }
    impl_->batch_input.clear();
    impl_->batch_spectrum.clear();
  } else {
    if (!impl_->fft_backend->prepare({config.chirp_segmentation.segment_fft_length,
                                      transform_count}, error)) {
      return false;
    }
    impl_->batch_input.resize(static_cast<std::size_t>(
        config.chirp_segmentation.segment_fft_length) * transform_count);
    impl_->batch_spectrum.reserve(
        (static_cast<std::size_t>(config.chirp_segmentation.segment_fft_length) / 2U + 1U) *
        transform_count);
  }
  impl_->configured = true;
  error.clear();
  return true;
}

bool SignalProcessor::updateRuntimeConfig(const ProcessingConfig& config,
                                          std::uint64_t processing_config_revision, std::string& error) {
  if (!impl_->configured) {
    error = "Configure the signal processor before updating runtime settings";
    return false;
  }
  auto candidate = impl_->config;
  candidate.processing = config;
  const auto validation = ConfigValidator::validate(candidate);
  if (validation.hasErrors()) {
    error = "Runtime processing settings failed validation";
    return false;
  }
  if (config.fft_backend != impl_->config.processing.fft_backend ||
      config.queue_capacity != impl_->config.processing.queue_capacity ||
      config.overflow_policy != impl_->config.processing.overflow_policy) {
    error = "FFT backend, queue capacity, and overflow policy require a stopped processing service";
    return false;
  }
  if (impl_->cuda_pipeline != nullptr &&
      !impl_->cuda_pipeline->updateRuntimeConfig(config, processing_config_revision, error)) {
    return false;
  }
  impl_->config.processing = config;
  impl_->processing_config_revision = processing_config_revision;
  error.clear();
  return true;
}

bool SignalProcessor::process(const RawFrame& raw, ProcessedFrame& processed, std::string& error) {
  if (!impl_->configured) {
    error = "Signal processor is not configured";
    return false;
  }
  if (!rawMatchesConfig(raw, impl_->config)) {
    error = "Raw frame does not match the configured full-period segmentation contract";
    return false;
  }
  const auto started = std::chrono::steady_clock::now();
  std::vector<float> up_input;
  std::vector<float> down_input;
  if (!preprocessSegment(raw, raw.metadata.up_segment, impl_->config.processing,
                         impl_->config.chirp_segmentation.polarity, false, impl_->up_window,
                         impl_->config.chirp_segmentation.segment_fft_length, up_input, error) ||
      !preprocessSegment(raw, raw.metadata.down_segment, impl_->config.processing,
                         impl_->config.chirp_segmentation.polarity, true, impl_->down_window,
                         impl_->config.chirp_segmentation.segment_fft_length, down_input, error)) {
    return false;
  }
  std::vector<std::complex<float>> up_spectrum;
  std::vector<std::complex<float>> down_spectrum;
  if (!impl_->fft_backend->prepare({impl_->config.chirp_segmentation.segment_fft_length, 1U}, error) ||
      !impl_->fft_backend->execute(up_input, up_spectrum, error) ||
      !impl_->fft_backend->execute(down_input, down_spectrum, error)) {
    return false;
  }

  initializeProcessed(raw, impl_->processing_config_revision, processed);
  processed.up_fft_magnitude_db = magnitudeDb(up_spectrum, impl_->up_window_sum);
  processed.down_fft_magnitude_db = magnitudeDb(down_spectrum, impl_->down_window_sum);
  processed.up_peak = detectPeak(processed.up_fft_magnitude_db,
                                 impl_->config.processing.peak_search_start_bin,
                                 impl_->config.processing.peak_search_end_bin,
                                 impl_->config.processing.peak_threshold_db);
  processed.down_peak = detectPeak(processed.down_fft_magnitude_db,
                                   impl_->config.processing.peak_search_start_bin,
                                   impl_->config.processing.peak_search_end_bin,
                                   impl_->config.processing.peak_threshold_db);

  finalizeMeasurement(raw, impl_->config, processed);
  processed.processing_latency_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  processed.processing_note = impl_->fft_backend->name();
  error.clear();
  return true;
}

bool SignalProcessor::processBatch(const RawFrameBatch& raw_batch,
                                   std::uint32_t selected_record_index,
                                   std::vector<ProcessedFrame>& processed_batch,
                                   std::string& error) {
  if (!impl_->configured) {
    error = "Configure the signal processor before processing a DMA batch";
    return false;
  }
  if (raw_batch.records.empty() ||
      raw_batch.records.size() > impl_->config.digitizer.records_per_buffer) {
    error = "Raw DMA batch record count is empty or exceeds the configured FFT batch";
    return false;
  }
  if (impl_->cuda_pipeline != nullptr) {
    return impl_->cuda_pipeline->processBatch(raw_batch, selected_record_index,
                                              processed_batch, error);
  }

  const auto started = std::chrono::steady_clock::now();
  const auto record_count = raw_batch.records.size();
  const auto fft_length = static_cast<std::size_t>(
      impl_->config.chirp_segmentation.segment_fft_length);
  const auto spectrum_length = fft_length / 2U + 1U;
  processed_batch.resize(record_count);
  for (std::size_t chunk_start = 0; chunk_start < record_count;
       chunk_start += impl_->batch_record_capacity) {
    const auto chunk_record_count = std::min(impl_->batch_record_capacity,
                                             record_count - chunk_start);
    const auto planned_record_count = record_count >= impl_->batch_record_capacity
        ? impl_->batch_record_capacity
        : chunk_record_count;
    const auto chunk_transform_count = planned_record_count * 2U;
    impl_->batch_input.resize(fft_length * chunk_transform_count);
    std::fill(impl_->batch_input.begin(), impl_->batch_input.end(), 0.0F);
    std::atomic<bool> preprocess_failed{false};
#if FMCW_HAS_OPENMP
    const auto batch_thread_count = openMpBatchThreadCount(chunk_record_count);
#pragma omp parallel for schedule(static) num_threads(batch_thread_count)
#endif
    for (std::int64_t local_record = 0;
         local_record < static_cast<std::int64_t>(chunk_record_count); ++local_record) {
#if FMCW_HAS_OPENMP
      prioritizeCurrentRealtimeThread(RealtimeThreadPriority::High);
#endif
      const auto local_index = static_cast<std::size_t>(local_record);
      const auto record_index = chunk_start + local_index;
      const auto& raw = raw_batch.records[record_index];
      if (!rawMatchesConfig(raw, impl_->config)) {
        preprocess_failed.store(true, std::memory_order_relaxed);
        continue;
      }
      auto* up_output = impl_->batch_input.data() + (local_index * 2U) * fft_length;
      auto* down_output = up_output + fft_length;
      std::string local_error;
      if (!preprocessSegmentInto(raw, raw.metadata.up_segment, impl_->config.processing,
                                 impl_->config.chirp_segmentation.polarity, false, impl_->up_window,
                                 fft_length, up_output, local_error) ||
          !preprocessSegmentInto(raw, raw.metadata.down_segment, impl_->config.processing,
                                 impl_->config.chirp_segmentation.polarity, true, impl_->down_window,
                                 fft_length, down_output, local_error)) {
        preprocess_failed.store(true, std::memory_order_relaxed);
      }
    }
    if (preprocess_failed.load(std::memory_order_relaxed)) {
      error = "Raw DMA batch preprocessing failed its segmentation contract";
      return false;
    }

    if (!impl_->fft_backend->prepare({fft_length, chunk_transform_count}, error) ||
        !impl_->fft_backend->execute(impl_->batch_input, impl_->batch_spectrum, error) ||
        impl_->batch_spectrum.size() != spectrum_length * chunk_transform_count) {
      if (error.empty()) {
        error = "FFT backend returned an invalid plan-many spectrum size";
      }
      return false;
    }

#if FMCW_HAS_OPENMP
#pragma omp parallel for schedule(static) num_threads(batch_thread_count)
#endif
    for (std::int64_t local_record = 0;
         local_record < static_cast<std::int64_t>(chunk_record_count); ++local_record) {
#if FMCW_HAS_OPENMP
      prioritizeCurrentRealtimeThread(RealtimeThreadPriority::High);
#endif
      const auto local_index = static_cast<std::size_t>(local_record);
      const auto record_index = chunk_start + local_index;
      const auto& raw = raw_batch.records[record_index];
      auto& processed = processed_batch[record_index];
      initializeProcessed(raw, impl_->processing_config_revision, processed);
      const auto* up_spectrum = impl_->batch_spectrum.data() +
          (local_index * 2U) * spectrum_length;
      const auto* down_spectrum = up_spectrum + spectrum_length;
      if (raw.metadata.record_index_in_buffer == selected_record_index) {
        processed.up_fft_magnitude_db = magnitudeDb(up_spectrum, spectrum_length,
                                                    impl_->up_window_sum);
        processed.down_fft_magnitude_db = magnitudeDb(down_spectrum, spectrum_length,
                                                      impl_->down_window_sum);
      }
      processed.up_peak = detectPeak(up_spectrum, spectrum_length, impl_->up_window_sum,
                                     impl_->config.processing.peak_search_start_bin,
                                     impl_->config.processing.peak_search_end_bin,
                                     impl_->config.processing.peak_threshold_db);
      processed.down_peak = detectPeak(down_spectrum, spectrum_length, impl_->down_window_sum,
                                       impl_->config.processing.peak_search_start_bin,
                                       impl_->config.processing.peak_search_end_bin,
                                       impl_->config.processing.peak_threshold_db);
      finalizeMeasurement(raw, impl_->config, processed);
    }
  }

  const auto batch_latency_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  const auto average_record_latency_ms = batch_latency_ms / static_cast<double>(record_count);
  const char* processing_note = impl_->fft_backend->kind() == FftBackendKind::Fftw
      ? "FFTW3f batch"
      : "cuFFT batch";
  for (auto& processed : processed_batch) {
    processed.processing_latency_ms = average_record_latency_ms;
    processed.processing_note = processing_note;
  }
  error.clear();
  return true;
}

bool SignalProcessor::supportsAsyncBatchProcessing() const {
  return impl_->configured && impl_->cuda_pipeline != nullptr;
}

std::size_t SignalProcessor::asyncBatchCapacity() const {
  return supportsAsyncBatchProcessing() ? impl_->cuda_pipeline->capacity() : 0U;
}

std::size_t SignalProcessor::inFlightBatchCount() const {
  return supportsAsyncBatchProcessing() ? impl_->cuda_pipeline->inFlightCount() : 0U;
}

bool SignalProcessor::submitBatch(RawFrameBatchPtr raw_batch,
                                  std::uint32_t selected_record_index,
                                  std::string& error) {
  if (!supportsAsyncBatchProcessing()) {
    error = "Asynchronous batch submission requires the CUDA signal pipeline";
    return false;
  }
  return impl_->cuda_pipeline->submitBatch(std::move(raw_batch), selected_record_index, error);
}

bool SignalProcessor::releaseCompletedBatchInputs(bool wait_for_oldest,
                                                   std::string& error) {
  if (!supportsAsyncBatchProcessing()) {
    error = "Asynchronous input release requires the CUDA signal pipeline";
    return false;
  }
  return impl_->cuda_pipeline->releaseCompletedInputs(wait_for_oldest, error);
}

bool SignalProcessor::collectNextBatch(bool wait,
                                       RawFrameBatchPtr& raw_batch,
                                       std::vector<ProcessedFrame>& processed_batch,
                                       bool& collected,
                                       std::string& error) {
  if (!supportsAsyncBatchProcessing()) {
    error = "Asynchronous batch collection requires the CUDA signal pipeline";
    return false;
  }
  return impl_->cuda_pipeline->collectNext(wait, raw_batch, processed_batch, collected, error);
}

std::string SignalProcessor::backendName() const {
  return impl_->fft_backend == nullptr ? std::string{} : impl_->fft_backend->name();
}

std::uint64_t SignalProcessor::processingConfigRevision() const {
  return impl_->processing_config_revision;
}

}  // namespace fmcw
