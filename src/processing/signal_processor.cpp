#include "processing/signal_processor.h"

#include "core/config_validation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace fmcw {
namespace {

constexpr double kSpeedOfLightMps = 299792458.0;
constexpr double kPi = 3.14159265358979323846;

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

bool preprocessSegment(const RawFrame& raw, SegmentRange range, const ProcessingConfig& processing,
                       SegmentPolarity polarity, bool down_segment, const std::vector<float>& window,
                       std::size_t fft_length, std::vector<float>& output, std::string& error) {
  if (!range.validFor(static_cast<std::uint32_t>(raw.samples.size())) || window.size() != range.length() ||
      fft_length < range.length()) {
    error = "Segment preprocessing received an invalid range, window, or FFT length";
    return false;
  }
  output.assign(fft_length, 0.0F);
  double mean = 0.0;
  if (processing.dc_removal) {
    for (std::uint32_t index = range.start_sample; index < range.end_sample_exclusive; ++index) {
      mean += static_cast<double>(raw.samples[index]) / 32768.0;
    }
    mean /= static_cast<double>(range.length());
  }
  for (std::size_t local = 0; local < range.length(); ++local) {
    float value = static_cast<float>(static_cast<double>(raw.samples[range.start_sample + local]) / 32768.0 - mean);
    if (down_segment && polarity == SegmentPolarity::InvertDown) {
      value = -value;
    }
    output[local] = value;
  }
  for (std::size_t local = 0; local < range.length(); ++local) {
    output[local] *= window[local];
  }
  error.clear();
  return true;
}

std::vector<float> magnitudeDb(const std::vector<std::complex<float>>& spectrum, float coherent_sum) {
  std::vector<float> result(spectrum.size(), -200.0F);
  const float scale = 2.0F / std::max(coherent_sum, 1.0F);
  for (std::size_t index = 0; index < spectrum.size(); ++index) {
    const float amplitude = std::max(std::abs(spectrum[index]) * scale, 1.0e-10F);
    result[index] = 20.0F * std::log10(amplitude);
  }
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

}  // namespace

struct SignalProcessor::Impl {
  explicit Impl(std::unique_ptr<IFftBackend> backend) : fft_backend(std::move(backend)) {}

  std::unique_ptr<IFftBackend> fft_backend;
  SystemConfig config;
  std::uint64_t processing_config_revision = 0;
  std::vector<float> up_window;
  std::vector<float> down_window;
  float up_window_sum = 1.0F;
  float down_window_sum = 1.0F;
  bool configured = false;
};

SignalProcessor::SignalProcessor(std::unique_ptr<IFftBackend> fft_backend)
    : impl_(std::make_unique<Impl>(std::move(fft_backend))) {}

SignalProcessor::~SignalProcessor() = default;

bool SignalProcessor::configure(const SystemConfig& config, std::uint64_t processing_config_revision,
                                std::string& error) {
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
  if (!impl_->fft_backend->prepare({config.chirp_segmentation.segment_fft_length, 1U}, error)) {
    return false;
  }
  impl_->config = config;
  impl_->processing_config_revision = processing_config_revision;
  impl_->up_window = makeWindow(config.chirp_segmentation.window, config.chirp_segmentation.up_segment.length());
  impl_->down_window = makeWindow(config.chirp_segmentation.window, config.chirp_segmentation.down_segment.length());
  impl_->up_window_sum = windowSum(impl_->up_window);
  impl_->down_window_sum = windowSum(impl_->down_window);
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
  if (raw.metadata.frame_kind != FrameKind::FullChirpPeriod ||
      raw.samples.size() != raw.metadata.record_length ||
      raw.metadata.up_segment.start_sample != impl_->config.chirp_segmentation.up_segment.start_sample ||
      raw.metadata.up_segment.end_sample_exclusive != impl_->config.chirp_segmentation.up_segment.end_sample_exclusive ||
      raw.metadata.down_segment.start_sample != impl_->config.chirp_segmentation.down_segment.start_sample ||
      raw.metadata.down_segment.end_sample_exclusive != impl_->config.chirp_segmentation.down_segment.end_sample_exclusive) {
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
  if (!impl_->fft_backend->execute(up_input, up_spectrum, error) ||
      !impl_->fft_backend->execute(down_input, down_spectrum, error)) {
    return false;
  }

  processed = {};
  processed.frame_id = raw.metadata.frame_id;
  processed.source_timestamp_ns = raw.metadata.host_timestamp_ns;
  processed.config_revision = raw.metadata.config_revision;
  processed.processing_config_revision = impl_->processing_config_revision;
  processed.scan_position = raw.metadata.scan_position;
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

  if (processed.up_peak.valid && processed.down_peak.valid) {
    const double bin_frequency_hz = raw.metadata.sample_rate_hz /
        static_cast<double>(impl_->config.chirp_segmentation.segment_fft_length);
    const double up_frequency_hz = processed.up_peak.peak_bin * bin_frequency_hz;
    const double down_frequency_hz = processed.down_peak.peak_bin * bin_frequency_hz;
    const double wavelength_m = impl_->config.calibration.velocity_wavelength_nm * 1.0e-9;
    // A full triangular sweep has chirp slope 2 * bandwidth * repetition rate.
    const double raw_distance = kSpeedOfLightMps * (up_frequency_hz + down_frequency_hz) /
        (8.0 * impl_->config.laser.sweep_bandwidth_hz * impl_->config.laser.sweep_rate_hz);
    const double raw_velocity = wavelength_m * (up_frequency_hz - down_frequency_hz) / 4.0;
    processed.distance_m = static_cast<float>(raw_distance * impl_->config.calibration.distance_scale +
        impl_->config.calibration.distance_offset_m);
    processed.velocity_mps = static_cast<float>(raw_velocity * impl_->config.calibration.velocity_scale +
        impl_->config.calibration.velocity_offset_mps);
    processed.measurement_valid = true;
    processed.point = toPoint(processed.distance_m, processed.velocity_mps,
                              0.5F * (processed.up_peak.magnitude_db + processed.down_peak.magnitude_db),
                              processed.scan_position, impl_->config.calibration);
  }
  processed.processing_latency_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  processed.processing_note = impl_->fft_backend->name();
  error.clear();
  return true;
}

std::string SignalProcessor::backendName() const {
  return impl_->fft_backend == nullptr ? std::string{} : impl_->fft_backend->name();
}

std::uint64_t SignalProcessor::processingConfigRevision() const {
  return impl_->processing_config_revision;
}

}  // namespace fmcw
