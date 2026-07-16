#include "processing/cuda/cuda_signal_pipeline.h"
#include "processing/cuda/cuda_module_policy.h"
#include "core/raw_frame_batch_pool.h"

#include <cuda_runtime.h>
#include <cufft.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <vector>

namespace fmcw {
namespace {

constexpr int kThreadsPerBlock = 256;
constexpr double kSpeedOfLightMps = 299792458.0;
constexpr double kPi = 3.14159265358979323846;

struct DevicePeakResult {
  std::int32_t index = -1;
  float magnitude_db = 0.0F;
  std::int32_t valid = 0;
};

struct DeviceRecordPosition {
  double sample_rate_hz = 0.0;
  float x_angle_deg = 0.0F;
  float y_angle_deg = 0.0F;
  std::int32_t valid = 0;
};

struct DeviceMeasurementResult {
  float distance_m = 0.0F;
  float velocity_mps = 0.0F;
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float intensity = 0.0F;
  std::int32_t measurement_valid = 0;
  std::int32_t point_valid = 0;
};

std::string cudaError(cudaError_t result, const char* operation) {
  return std::string(operation) + " failed: " + cudaGetErrorString(result);
}

std::string cufftError(cufftResult result, const char* operation) {
  return std::string(operation) + " failed with cuFFT code " +
      std::to_string(static_cast<int>(result));
}

template <typename T>
cudaError_t allocateDevice(T** pointer, std::size_t count) {
  return cudaMalloc(reinterpret_cast<void**>(pointer), count * sizeof(T));
}

template <typename T>
cudaError_t allocatePinned(T** pointer, std::size_t count) {
  return cudaMallocHost(reinterpret_cast<void**>(pointer), count * sizeof(T));
}

__device__ float normalizedSample(std::uint16_t stored_sample, int sample_format) {
  int signed_sample = 0;
  if (sample_format == static_cast<int>(SampleFormat::UnsignedOffsetBinary12LeftAligned)) {
    signed_sample = static_cast<int>(stored_sample & 0xFFF0U) - 32768;
  } else {
    signed_sample = stored_sample >= 0x8000U
        ? static_cast<int>(stored_sample) - 65536
        : static_cast<int>(stored_sample);
  }
  return static_cast<float>(signed_sample) / 32768.0F;
}

__global__ void segmentMeansKernel(const std::uint16_t* raw,
                                   float* means,
                                   int record_length,
                                   int up_start,
                                   int up_length,
                                   int down_start,
                                   int down_length,
                                   bool dc_removal,
                                   int sample_format) {
  const int transform = static_cast<int>(blockIdx.x);
  const bool down = (transform & 1) != 0;
  const int record = transform / 2;
  const int segment_start = down ? down_start : up_start;
  const int segment_length = down ? down_length : up_length;
  extern __shared__ float partial[];

  float sum = 0.0F;
  if (dc_removal) {
    const auto* samples = raw + static_cast<std::size_t>(record) * record_length + segment_start;
    for (int local = static_cast<int>(threadIdx.x); local < segment_length;
         local += static_cast<int>(blockDim.x)) {
      sum += normalizedSample(samples[local], sample_format);
    }
  }
  partial[threadIdx.x] = sum;
  __syncthreads();
  for (int stride = static_cast<int>(blockDim.x) / 2; stride > 0; stride >>= 1) {
    if (static_cast<int>(threadIdx.x) < stride) {
      partial[threadIdx.x] += partial[threadIdx.x + stride];
    }
    __syncthreads();
  }
  if (threadIdx.x == 0U) {
    means[transform] = dc_removal ? partial[0] / static_cast<float>(segment_length) : 0.0F;
  }
}

__global__ void preprocessSegmentsKernel(const std::uint16_t* raw,
                                         const float* means,
                                         const float* up_window,
                                         const float* down_window,
                                         float* fft_input,
                                         std::size_t total_output_count,
                                         int record_length,
                                         int fft_length,
                                         int up_start,
                                         int up_length,
                                         int down_start,
                                         int down_length,
                                         bool invert_down,
                                         int sample_format) {
  const auto output_index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (output_index >= total_output_count) {
    return;
  }
  const auto transform = output_index / static_cast<std::size_t>(fft_length);
  const int local = static_cast<int>(output_index % static_cast<std::size_t>(fft_length));
  const bool down = (transform & 1U) != 0U;
  const auto record = transform / 2U;
  const int segment_start = down ? down_start : up_start;
  const int segment_length = down ? down_length : up_length;
  const auto* window = down ? down_window : up_window;

  float value = 0.0F;
  if (local < segment_length) {
    const auto sample_index = record * static_cast<std::size_t>(record_length) +
        static_cast<std::size_t>(segment_start + local);
    value = normalizedSample(raw[sample_index], sample_format) - means[transform];
    if (down && invert_down) {
      value = -value;
    }
    value *= window[local];
  }
  fft_input[output_index] = value;
}

__global__ void detectPeaksKernel(const cufftComplex* spectrum,
                                  DevicePeakResult* peaks,
                                  int spectrum_length,
                                  int transform_count,
                                  int start_bin,
                                  int end_bin,
                                  float threshold_db,
                                  float up_window_sum,
                                  float down_window_sum) {
  const int transform = static_cast<int>(blockIdx.x);
  if (transform >= transform_count) {
    return;
  }
  extern __shared__ unsigned char shared_bytes[];
  auto* shared_power = reinterpret_cast<float*>(shared_bytes);
  auto* shared_index = reinterpret_cast<int*>(shared_power + blockDim.x);
  const auto* row = spectrum + static_cast<std::size_t>(transform) * spectrum_length;

  float best_power = -1.0F;
  int best_index = -1;
  for (int index = start_bin + static_cast<int>(threadIdx.x); index <= end_bin;
       index += static_cast<int>(blockDim.x)) {
    const float re = row[index].x;
    const float im = row[index].y;
    const float power = re * re + im * im;
    if (power > best_power || (power == best_power && (best_index < 0 || index < best_index))) {
      best_power = power;
      best_index = index;
    }
  }
  shared_power[threadIdx.x] = best_power;
  shared_index[threadIdx.x] = best_index;
  __syncthreads();

  for (int stride = static_cast<int>(blockDim.x) / 2; stride > 0; stride >>= 1) {
    if (static_cast<int>(threadIdx.x) < stride) {
      const float other_power = shared_power[threadIdx.x + stride];
      const int other_index = shared_index[threadIdx.x + stride];
      const bool replace = other_power > shared_power[threadIdx.x] ||
          (other_power == shared_power[threadIdx.x] && other_index >= 0 &&
           (shared_index[threadIdx.x] < 0 || other_index < shared_index[threadIdx.x]));
      if (replace) {
        shared_power[threadIdx.x] = other_power;
        shared_index[threadIdx.x] = other_index;
      }
    }
    __syncthreads();
  }

  if (threadIdx.x == 0U) {
    DevicePeakResult result;
    const int index = shared_index[0];
    if (index >= 0) {
      const float coherent_sum = (transform & 1) == 0 ? up_window_sum : down_window_sum;
      const float amplitude = fmaxf(hypotf(row[index].x, row[index].y) *
                                    (2.0F / fmaxf(coherent_sum, 1.0F)), 1.0e-10F);
      const float magnitude_db = 20.0F * log10f(amplitude);
      if (isfinite(magnitude_db) && magnitude_db > threshold_db) {
        result.index = index;
        result.magnitude_db = magnitude_db;
        result.valid = 1;
      }
    }
    peaks[transform] = result;
  }
}

__global__ void selectedMagnitudeKernel(const cufftComplex* spectrum,
                                        float* selected_magnitude,
                                        int spectrum_length,
                                        int selected_record,
                                        float up_window_sum,
                                        float down_window_sum) {
  const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  const auto total = static_cast<std::size_t>(spectrum_length) * 2U;
  if (index >= total) {
    return;
  }
  const int segment = static_cast<int>(index / static_cast<std::size_t>(spectrum_length));
  const int bin = static_cast<int>(index % static_cast<std::size_t>(spectrum_length));
  const int transform = selected_record * 2 + segment;
  const auto value = spectrum[static_cast<std::size_t>(transform) * spectrum_length + bin];
  const float coherent_sum = segment == 0 ? up_window_sum : down_window_sum;
  const float amplitude = fmaxf(hypotf(value.x, value.y) *
                                (2.0F / fmaxf(coherent_sum, 1.0F)), 1.0e-10F);
  selected_magnitude[index] = 20.0F * log10f(amplitude);
}

__global__ void measurementsKernel(const DevicePeakResult* peaks,
                                   const DeviceRecordPosition* positions,
                                   DeviceMeasurementResult* measurements,
                                   int record_count,
                                   int fft_length,
                                   double bandwidth_hz,
                                   double sweep_rate_hz,
                                   double wavelength_nm,
                                   double distance_scale,
                                   double distance_offset_m,
                                   double velocity_scale,
                                   double velocity_offset_mps,
                                   double x_angle_offset_deg,
                                   double y_angle_offset_deg) {
  const int record = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
  if (record >= record_count) {
    return;
  }
  DeviceMeasurementResult result;
  const float invalid = nanf("");
  result.distance_m = invalid;
  result.velocity_mps = invalid;
  result.x = invalid;
  result.y = invalid;
  result.z = invalid;
  result.intensity = invalid;

  const auto up = peaks[record * 2];
  const auto down = peaks[record * 2 + 1];
  if (up.valid == 0 || down.valid == 0) {
    measurements[record] = result;
    return;
  }

  const auto position = positions[record];
  const double bin_frequency_hz = position.sample_rate_hz / static_cast<double>(fft_length);
  const double up_frequency_hz = static_cast<double>(up.index) * bin_frequency_hz;
  const double down_frequency_hz = static_cast<double>(down.index) * bin_frequency_hz;
  const double raw_distance = kSpeedOfLightMps * (up_frequency_hz + down_frequency_hz) /
      (8.0 * bandwidth_hz * sweep_rate_hz);
  const double raw_velocity = wavelength_nm * 1.0e-9 *
      (up_frequency_hz - down_frequency_hz) / 4.0;
  const double distance = raw_distance * distance_scale + distance_offset_m;
  const double velocity = raw_velocity * velocity_scale + velocity_offset_mps;
  result.distance_m = static_cast<float>(distance);
  result.velocity_mps = static_cast<float>(velocity);
  result.intensity = 0.5F * (up.magnitude_db + down.magnitude_db);
  result.measurement_valid = 1;

  if (position.valid != 0 && isfinite(distance) && isfinite(velocity) &&
      isfinite(static_cast<double>(position.x_angle_deg)) &&
      isfinite(static_cast<double>(position.y_angle_deg))) {
    const double x_angle = (static_cast<double>(position.x_angle_deg) + x_angle_offset_deg) *
        kPi / 180.0;
    const double y_angle = (static_cast<double>(position.y_angle_deg) + y_angle_offset_deg) *
        kPi / 180.0;
    const double horizontal_range = distance * cos(y_angle);
    result.x = static_cast<float>(horizontal_range * sin(x_angle));
    result.y = static_cast<float>(horizontal_range * cos(x_angle));
    result.z = static_cast<float>(-distance * sin(y_angle));
    result.point_valid = 1;
  }
  measurements[record] = result;
}

}  // namespace

struct CudaSignalPipeline::Impl {
  static constexpr std::size_t kSlotCount = 1U;

  struct Slot {
    cudaStream_t stream = nullptr;
    cudaEvent_t input_consumed_event = nullptr;
    cudaEvent_t completion_event = nullptr;
    cufftHandle plan = 0;
    std::size_t planned_transform_count = 0U;
    std::uint16_t* host_input = nullptr;
    DeviceRecordPosition* host_positions = nullptr;
    DevicePeakResult* host_peaks = nullptr;
    DeviceMeasurementResult* host_measurements = nullptr;
    float* host_selected_magnitude = nullptr;
    std::uint16_t* device_input = nullptr;
    DeviceRecordPosition* device_positions = nullptr;
    float* device_up_window = nullptr;
    float* device_down_window = nullptr;
    float* device_means = nullptr;
    float* device_fft_input = nullptr;
    cufftComplex* device_spectrum = nullptr;
    DevicePeakResult* device_peaks = nullptr;
    DeviceMeasurementResult* device_measurements = nullptr;
    float* device_selected_magnitude = nullptr;
    RawFrameBatchPtr input_batch;
    MutableRawFrameBatchPtr result_batch;
    std::chrono::steady_clock::time_point started;
    std::uint64_t processing_config_revision = 0U;
    std::size_t record_count = 0U;
    int selected_record = -1;
    SampleFormat sample_format = SampleFormat::SignedInt16;
    bool busy = false;

    void releasePlan() {
      if (plan != 0) {
        cufftDestroy(plan);
        plan = 0;
      }
      planned_transform_count = 0U;
    }

    bool ensurePlan(std::size_t fft_length, std::size_t transform_count,
                    std::string& error) {
      if (plan != 0 && planned_transform_count == transform_count) {
        return true;
      }
      releasePlan();
      const auto plan_result = cufftPlan1d(&plan, static_cast<int>(fft_length), CUFFT_R2C,
                                           static_cast<int>(transform_count));
      if (plan_result != CUFFT_SUCCESS) {
        error = cufftError(plan_result, "cufftPlan1d full signal batch");
        return false;
      }
      const auto stream_result = cufftSetStream(plan, stream);
      if (stream_result != CUFFT_SUCCESS) {
        error = cufftError(stream_result, "cufftSetStream full signal batch");
        releasePlan();
        return false;
      }
      planned_transform_count = transform_count;
      return true;
    }

    void release() {
      if (stream != nullptr) cudaStreamSynchronize(stream);
      input_batch.reset();
      result_batch.reset();
      busy = false;
      releasePlan();
      if (device_selected_magnitude != nullptr) cudaFree(device_selected_magnitude);
      if (device_measurements != nullptr) cudaFree(device_measurements);
      if (device_peaks != nullptr) cudaFree(device_peaks);
      if (device_spectrum != nullptr) cudaFree(device_spectrum);
      if (device_fft_input != nullptr) cudaFree(device_fft_input);
      if (device_means != nullptr) cudaFree(device_means);
      if (device_down_window != nullptr) cudaFree(device_down_window);
      if (device_up_window != nullptr) cudaFree(device_up_window);
      if (device_positions != nullptr) cudaFree(device_positions);
      if (device_input != nullptr) cudaFree(device_input);
      if (host_selected_magnitude != nullptr) cudaFreeHost(host_selected_magnitude);
      if (host_measurements != nullptr) cudaFreeHost(host_measurements);
      if (host_peaks != nullptr) cudaFreeHost(host_peaks);
      if (host_positions != nullptr) cudaFreeHost(host_positions);
      if (host_input != nullptr) cudaFreeHost(host_input);
      if (completion_event != nullptr) cudaEventDestroy(completion_event);
      if (input_consumed_event != nullptr) cudaEventDestroy(input_consumed_event);
      if (stream != nullptr) cudaStreamDestroy(stream);
      *this = {};
    }
  };

  SystemConfig config;
  std::uint64_t processing_config_revision = 0U;
  std::size_t maximum_records = 0U;
  std::size_t record_length = 0U;
  std::size_t fft_length = 0U;
  std::size_t spectrum_length = 0U;
  float up_window_sum = 1.0F;
  float down_window_sum = 1.0F;
  RawFrameBatchPool result_batch_pool{2U};
  std::array<Slot, kSlotCount> slots;
  std::deque<std::size_t> submitted_order;
  bool configured = false;

  void release() {
    for (auto& slot : slots) {
      slot.release();
    }
    submitted_order.clear();
    configured = false;
  }
};

CudaSignalPipeline::CudaSignalPipeline() : impl_(std::make_unique<Impl>()) {}
CudaSignalPipeline::~CudaSignalPipeline() { impl_->release(); }

bool CudaSignalPipeline::available() {
  configureEagerCudaModuleLoading();
  int device_count = 0;
  return cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0;
}

bool CudaSignalPipeline::configure(const SystemConfig& config,
                                   std::uint64_t processing_config_revision,
                                   const std::vector<float>& up_window,
                                   const std::vector<float>& down_window,
                                   float up_window_sum,
                                   float down_window_sum,
                                   std::string& error) {
  if (!available()) {
    error = "No CUDA device is available for the full signal pipeline";
    return false;
  }
  if (up_window.size() != config.chirp_segmentation.up_segment.length() ||
      down_window.size() != config.chirp_segmentation.down_segment.length()) {
    error = "CUDA signal pipeline window lengths do not match chirp segmentation";
    return false;
  }

  impl_->release();
  impl_->config = config;
  impl_->processing_config_revision = processing_config_revision;
  impl_->maximum_records = config.digitizer.records_per_buffer;
  impl_->record_length = config.digitizer.sample_point;
  impl_->fft_length = config.chirp_segmentation.segment_fft_length;
  impl_->spectrum_length = impl_->fft_length / 2U + 1U;
  impl_->up_window_sum = up_window_sum;
  impl_->down_window_sum = down_window_sum;
  const auto maximum_transform_count = impl_->maximum_records * 2U;

  for (auto& slot : impl_->slots) {
    auto cuda_result = cudaStreamCreateWithFlags(&slot.stream, cudaStreamNonBlocking);
    if (cuda_result == cudaSuccess) {
      cuda_result = cudaEventCreateWithFlags(&slot.input_consumed_event, cudaEventDisableTiming);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = cudaEventCreateWithFlags(&slot.completion_event, cudaEventDisableTiming);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocatePinned(&slot.host_input, impl_->maximum_records * impl_->record_length);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocatePinned(&slot.host_positions, impl_->maximum_records);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocatePinned(&slot.host_peaks, maximum_transform_count);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocatePinned(&slot.host_measurements, impl_->maximum_records);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocatePinned(&slot.host_selected_magnitude, impl_->spectrum_length * 2U);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_input,
                                   impl_->maximum_records * impl_->record_length);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_positions, impl_->maximum_records);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_up_window, up_window.size());
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_down_window, down_window.size());
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_means, maximum_transform_count);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_fft_input,
                                   maximum_transform_count * impl_->fft_length);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_spectrum,
                                   maximum_transform_count * impl_->spectrum_length);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_peaks, maximum_transform_count);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_measurements, impl_->maximum_records);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = allocateDevice(&slot.device_selected_magnitude,
                                   impl_->spectrum_length * 2U);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = cudaMemcpyAsync(slot.device_up_window, up_window.data(),
                                    up_window.size() * sizeof(float), cudaMemcpyHostToDevice,
                                    slot.stream);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = cudaMemcpyAsync(slot.device_down_window, down_window.data(),
                                    down_window.size() * sizeof(float), cudaMemcpyHostToDevice,
                                    slot.stream);
    }
    if (cuda_result == cudaSuccess) {
      cuda_result = cudaStreamSynchronize(slot.stream);
    }
    if (cuda_result != cudaSuccess ||
        !slot.ensurePlan(impl_->fft_length, maximum_transform_count, error)) {
      if (cuda_result != cudaSuccess) {
        error = cudaError(cuda_result, "CUDA full signal pipeline allocation or window upload");
      }
      impl_->release();
      return false;
    }
  }
  impl_->configured = true;
  error.clear();
  return true;
}

bool CudaSignalPipeline::updateRuntimeConfig(const ProcessingConfig& config,
                                             std::uint64_t processing_config_revision,
                                             std::string& error) {
  if (!impl_->configured) {
    error = "Configure the CUDA signal pipeline before updating runtime settings";
    return false;
  }
  if (!impl_->submitted_order.empty()) {
    error = "Drain submitted CUDA batches before updating runtime settings";
    return false;
  }
  impl_->config.processing = config;
  impl_->processing_config_revision = processing_config_revision;
  error.clear();
  return true;
}

std::size_t CudaSignalPipeline::capacity() const {
  return impl_->configured ? Impl::kSlotCount : 0U;
}

std::size_t CudaSignalPipeline::inFlightCount() const {
  return impl_->submitted_order.size();
}

bool CudaSignalPipeline::submitBatch(RawFrameBatchPtr raw_batch,
                                     std::uint32_t selected_record_index,
                                     std::string& error) {
  if (!impl_->configured || !raw_batch || raw_batch->records.empty() ||
      raw_batch->records.size() > impl_->maximum_records) {
    error = "CUDA signal batch is empty, oversized, or not configured";
    return false;
  }
  if (impl_->submitted_order.size() >= Impl::kSlotCount) {
    error = "CUDA signal pipeline has no free asynchronous slot";
    return false;
  }
  auto slot_iterator = std::find_if(impl_->slots.begin(), impl_->slots.end(),
                                    [](const Impl::Slot& slot) { return !slot.busy; });
  if (slot_iterator == impl_->slots.end()) {
    error = "CUDA slot state is inconsistent with the in-flight queue";
    return false;
  }
  const auto slot_index = static_cast<std::size_t>(slot_iterator - impl_->slots.begin());
  auto& slot = *slot_iterator;
  const auto& batch = *raw_batch;
  const auto started = std::chrono::steady_clock::now();
  const auto record_count = batch.records.size();
  const auto transform_count = record_count * 2U;
  int selected_record = -1;
  for (std::size_t index = 0; index < record_count; ++index) {
    const auto& raw = batch.records[index];
    if (raw.metadata.frame_kind != FrameKind::FullChirpPeriod ||
        raw.samples.size() != impl_->record_length ||
        raw.metadata.record_length != impl_->record_length ||
        raw.metadata.up_segment.start_sample != impl_->config.chirp_segmentation.up_segment.start_sample ||
        raw.metadata.up_segment.end_sample_exclusive !=
            impl_->config.chirp_segmentation.up_segment.end_sample_exclusive ||
        raw.metadata.down_segment.start_sample != impl_->config.chirp_segmentation.down_segment.start_sample ||
        raw.metadata.down_segment.end_sample_exclusive !=
            impl_->config.chirp_segmentation.down_segment.end_sample_exclusive) {
      error = "CUDA raw batch does not match the full-period segmentation contract";
      return false;
    }
    if (raw.metadata.sample_format != batch.records.front().metadata.sample_format) {
      error = "CUDA raw batch contains mixed sample formats";
      return false;
    }
    slot.host_positions[index] = {raw.metadata.sample_rate_hz,
                                  raw.metadata.scan_position.x_angle_deg,
                                  raw.metadata.scan_position.y_angle_deg,
                                  raw.metadata.scan_position.valid ? 1 : 0};
    if (raw.metadata.record_index_in_buffer == selected_record_index) {
      selected_record = static_cast<int>(index);
    }
  }
  if (selected_record < 0) {
    error = "Selected record is not present in the CUDA raw batch";
    return false;
  }
  const std::uint16_t* source_samples = nullptr;
  if (batch.hasContiguousSamples() && batch.hasExternalSampleStorage()) {
    source_samples = reinterpret_cast<const std::uint16_t*>(batch.contiguous_samples.data());
  } else if (batch.hasContiguousSamples()) {
    std::memcpy(slot.host_input, batch.contiguous_samples.data(),
                batch.contiguous_samples.size() * sizeof(std::int16_t));
    source_samples = slot.host_input;
  } else {
    for (std::size_t index = 0; index < record_count; ++index) {
      std::memcpy(slot.host_input + index * impl_->record_length,
                  batch.records[index].samples.data(),
                  impl_->record_length * sizeof(std::int16_t));
    }
    source_samples = slot.host_input;
  }
  if (!slot.ensurePlan(impl_->fft_length, transform_count, error)) {
    return false;
  }

  auto result_batch = impl_->result_batch_pool.acquire();
  result_batch->metadata = batch.metadata;
  result_batch->records.resize(record_count);
  for (std::size_t index = 0; index < record_count; ++index) {
    result_batch->records[index].metadata = batch.records[index].metadata;
    if (static_cast<int>(index) == selected_record) {
      result_batch->records[index].samples.assign(batch.records[index].samples.begin(),
                                                  batch.records[index].samples.end());
    }
  }

  slot.input_batch = raw_batch;
  slot.result_batch = std::move(result_batch);
  slot.busy = true;
  struct SubmissionRollback {
    Impl::Slot* slot = nullptr;
    bool armed = true;
    ~SubmissionRollback() {
      if (!armed || slot == nullptr) {
        return;
      }
      cudaStreamSynchronize(slot->stream);
      slot->input_batch.reset();
      slot->result_batch.reset();
      slot->busy = false;
    }
  } rollback{&slot, true};

  auto cuda_result = cudaMemcpyAsync(slot.device_input, source_samples,
                                     record_count * impl_->record_length * sizeof(std::int16_t),
                                     cudaMemcpyHostToDevice, slot.stream);
  if (cuda_result == cudaSuccess) {
    cuda_result = cudaMemcpyAsync(slot.device_positions, slot.host_positions,
                                  record_count * sizeof(DeviceRecordPosition),
                                  cudaMemcpyHostToDevice, slot.stream);
  }
  if (cuda_result == cudaSuccess) {
    cuda_result = cudaEventRecord(slot.input_consumed_event, slot.stream);
  }
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "CUDA signal batch H2D");
    return false;
  }

  segmentMeansKernel<<<static_cast<unsigned int>(transform_count), kThreadsPerBlock,
                       kThreadsPerBlock * sizeof(float), slot.stream>>>(
      slot.device_input, slot.device_means, static_cast<int>(impl_->record_length),
      static_cast<int>(impl_->config.chirp_segmentation.up_segment.start_sample),
      static_cast<int>(impl_->config.chirp_segmentation.up_segment.length()),
      static_cast<int>(impl_->config.chirp_segmentation.down_segment.start_sample),
      static_cast<int>(impl_->config.chirp_segmentation.down_segment.length()),
      impl_->config.processing.dc_removal,
      static_cast<int>(batch.records.front().metadata.sample_format));
  cuda_result = cudaGetLastError();
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "CUDA segment mean kernel");
    return false;
  }

  const auto fft_input_count = transform_count * impl_->fft_length;
  const auto preprocess_blocks = static_cast<unsigned int>(
      (fft_input_count + kThreadsPerBlock - 1U) / kThreadsPerBlock);
  preprocessSegmentsKernel<<<preprocess_blocks, kThreadsPerBlock, 0, slot.stream>>>(
      slot.device_input, slot.device_means, slot.device_up_window,
      slot.device_down_window, slot.device_fft_input, fft_input_count,
      static_cast<int>(impl_->record_length), static_cast<int>(impl_->fft_length),
      static_cast<int>(impl_->config.chirp_segmentation.up_segment.start_sample),
      static_cast<int>(impl_->config.chirp_segmentation.up_segment.length()),
      static_cast<int>(impl_->config.chirp_segmentation.down_segment.start_sample),
      static_cast<int>(impl_->config.chirp_segmentation.down_segment.length()),
      impl_->config.chirp_segmentation.polarity == SegmentPolarity::InvertDown,
      static_cast<int>(batch.records.front().metadata.sample_format));
  cuda_result = cudaGetLastError();
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "CUDA segment preprocessing kernel");
    return false;
  }

  const auto fft_result = cufftExecR2C(slot.plan, slot.device_fft_input,
                                       slot.device_spectrum);
  if (fft_result != CUFFT_SUCCESS) {
    error = cufftError(fft_result, "cufftExecR2C full signal batch");
    return false;
  }

  const auto peak_shared_bytes = kThreadsPerBlock * (sizeof(float) + sizeof(int));
  detectPeaksKernel<<<static_cast<unsigned int>(transform_count), kThreadsPerBlock,
                      peak_shared_bytes, slot.stream>>>(
      slot.device_spectrum, slot.device_peaks, static_cast<int>(impl_->spectrum_length),
      static_cast<int>(transform_count),
      static_cast<int>(impl_->config.processing.peak_search_start_bin),
      static_cast<int>(impl_->config.processing.peak_search_end_bin),
      static_cast<float>(impl_->config.processing.peak_threshold_db),
      impl_->up_window_sum, impl_->down_window_sum);
  cuda_result = cudaGetLastError();
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "CUDA strict peak kernel");
    return false;
  }

  if (selected_record >= 0) {
    const auto selected_count = impl_->spectrum_length * 2U;
    const auto selected_blocks = static_cast<unsigned int>(
        (selected_count + kThreadsPerBlock - 1U) / kThreadsPerBlock);
    selectedMagnitudeKernel<<<selected_blocks, kThreadsPerBlock, 0, slot.stream>>>(
        slot.device_spectrum, slot.device_selected_magnitude,
        static_cast<int>(impl_->spectrum_length), selected_record,
        impl_->up_window_sum, impl_->down_window_sum);
    cuda_result = cudaGetLastError();
    if (cuda_result != cudaSuccess) {
      error = cudaError(cuda_result, "CUDA selected spectrum kernel");
      return false;
    }
  }

  const auto measurement_blocks = static_cast<unsigned int>(
      (record_count + kThreadsPerBlock - 1U) / kThreadsPerBlock);
  measurementsKernel<<<measurement_blocks, kThreadsPerBlock, 0, slot.stream>>>(
      slot.device_peaks, slot.device_positions, slot.device_measurements,
      static_cast<int>(record_count), static_cast<int>(impl_->fft_length),
      impl_->config.laser.sweep_bandwidth_hz, impl_->config.laser.sweep_rate_hz,
      impl_->config.calibration.velocity_wavelength_nm,
      impl_->config.calibration.distance_scale, impl_->config.calibration.distance_offset_m,
      impl_->config.calibration.velocity_scale, impl_->config.calibration.velocity_offset_mps,
      impl_->config.calibration.x_angle_offset_deg, impl_->config.calibration.y_angle_offset_deg);
  cuda_result = cudaGetLastError();
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "CUDA distance velocity XYZ kernel");
    return false;
  }

  cuda_result = cudaMemcpyAsync(slot.host_peaks, slot.device_peaks,
                                transform_count * sizeof(DevicePeakResult),
                                cudaMemcpyDeviceToHost, slot.stream);
  if (cuda_result == cudaSuccess) {
    cuda_result = cudaMemcpyAsync(slot.host_measurements, slot.device_measurements,
                                  record_count * sizeof(DeviceMeasurementResult),
                                  cudaMemcpyDeviceToHost, slot.stream);
  }
  if (cuda_result == cudaSuccess && selected_record >= 0) {
    cuda_result = cudaMemcpyAsync(slot.host_selected_magnitude,
                                  slot.device_selected_magnitude,
                                  impl_->spectrum_length * 2U * sizeof(float),
                                  cudaMemcpyDeviceToHost, slot.stream);
  }
  if (cuda_result == cudaSuccess) {
    cuda_result = cudaEventRecord(slot.completion_event, slot.stream);
  }
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "CUDA signal result enqueue");
    return false;
  }

  slot.started = started;
  slot.processing_config_revision = impl_->processing_config_revision;
  slot.record_count = record_count;
  slot.selected_record = selected_record;
  slot.sample_format = batch.records.front().metadata.sample_format;
  impl_->submitted_order.push_back(slot_index);
  rollback.armed = false;
  error.clear();
  return true;
}

bool CudaSignalPipeline::releaseCompletedInputs(bool wait_for_oldest,
                                                std::string& error) {
  if (!impl_->configured) {
    error = "Configure the CUDA signal pipeline before releasing DMA inputs";
    return false;
  }
  bool wait_consumed = false;
  for (const auto slot_index : impl_->submitted_order) {
    auto& slot = impl_->slots[slot_index];
    if (!slot.input_batch) {
      continue;
    }
    const bool wait_for_this_slot = wait_for_oldest && !wait_consumed;
    wait_consumed = wait_consumed || wait_for_this_slot;
    const auto event_result = wait_for_this_slot
        ? cudaEventSynchronize(slot.input_consumed_event)
        : cudaEventQuery(slot.input_consumed_event);
    if (!wait_for_this_slot && event_result == cudaErrorNotReady) {
      continue;
    }
    if (event_result != cudaSuccess) {
      error = cudaError(event_result, "CUDA H2D completion event");
      return false;
    }
    slot.input_batch.reset();
  }
  error.clear();
  return true;
}

bool CudaSignalPipeline::collectNext(bool wait,
                                     RawFrameBatchPtr& raw_batch,
                                     std::vector<ProcessedFrame>& processed_batch,
                                     bool& collected,
                                     std::string& error) {
  raw_batch.reset();
  processed_batch.clear();
  collected = false;
  if (!impl_->configured) {
    error = "Configure the CUDA signal pipeline before collecting a batch";
    return false;
  }
  if (impl_->submitted_order.empty()) {
    error.clear();
    return true;
  }

  const auto slot_index = impl_->submitted_order.front();
  auto& slot = impl_->slots[slot_index];
  const auto event_result = wait
      ? cudaEventSynchronize(slot.completion_event)
      : cudaEventQuery(slot.completion_event);
  if (!wait && event_result == cudaErrorNotReady) {
    error.clear();
    return true;
  }
  if (event_result != cudaSuccess) {
    cudaStreamSynchronize(slot.stream);
    impl_->submitted_order.pop_front();
    slot.input_batch.reset();
    slot.result_batch.reset();
    slot.busy = false;
    error = cudaError(event_result, "CUDA signal completion event");
    return false;
  }
  slot.input_batch.reset();
  if (!slot.busy || !slot.result_batch ||
      slot.record_count != slot.result_batch->records.size()) {
    impl_->submitted_order.pop_front();
    slot.result_batch.reset();
    slot.busy = false;
    error = "CUDA completed slot has inconsistent batch ownership";
    return false;
  }

  processed_batch.resize(slot.record_count);
  const auto assign_peak = [](const DevicePeakResult& source, PeakMeasurement& target) {
    if (source.valid != 0) {
      target.discrete_bin = source.index;
      target.peak_bin = static_cast<float>(source.index);
      target.magnitude_db = source.magnitude_db;
      target.state = PeakTrackState::Detected;
      target.valid = true;
    }
  };
  for (std::size_t index = 0; index < slot.record_count; ++index) {
    const auto& raw = slot.result_batch->records[index];
    auto& processed = processed_batch[index];
    processed = {};
    processed.frame_id = raw.metadata.frame_id;
    processed.source_timestamp_ns = raw.metadata.host_timestamp_ns;
    processed.config_revision = raw.metadata.config_revision;
    processed.processing_config_revision = slot.processing_config_revision;
    processed.scan_position = raw.metadata.scan_position;
    assign_peak(slot.host_peaks[index * 2U], processed.up_peak);
    assign_peak(slot.host_peaks[index * 2U + 1U], processed.down_peak);
    const auto& measurement = slot.host_measurements[index];
    if (measurement.measurement_valid != 0) {
      processed.distance_m = measurement.distance_m;
      processed.velocity_mps = measurement.velocity_mps;
      processed.measurement_valid = true;
    }
    if (measurement.point_valid != 0) {
      processed.point.x = measurement.x;
      processed.point.y = measurement.y;
      processed.point.z = measurement.z;
      processed.point.intensity = measurement.intensity;
      processed.point.velocity = measurement.velocity_mps;
      processed.point.valid = true;
    }
    if (static_cast<int>(index) == slot.selected_record) {
      processed.up_fft_magnitude_db.assign(slot.host_selected_magnitude,
                                           slot.host_selected_magnitude + impl_->spectrum_length);
      processed.down_fft_magnitude_db.assign(
          slot.host_selected_magnitude + impl_->spectrum_length,
          slot.host_selected_magnitude + impl_->spectrum_length * 2U);
    }
    processed.processing_note = "cuFFT async batch";
  }

  const auto batch_latency_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - slot.started).count();
  const auto average_record_latency_ms = batch_latency_ms /
      static_cast<double>(slot.record_count);
  for (auto& processed : processed_batch) {
    processed.processing_latency_ms = average_record_latency_ms;
  }

  raw_batch = std::move(slot.result_batch);
  slot.record_count = 0U;
  slot.selected_record = -1;
  slot.busy = false;
  impl_->submitted_order.pop_front();
  collected = true;
  error.clear();
  return true;
}

bool CudaSignalPipeline::processBatch(const RawFrameBatch& raw_batch,
                                      std::uint32_t selected_record_index,
                                      std::vector<ProcessedFrame>& processed_batch,
                                      std::string& error) {
  if (inFlightCount() != 0U) {
    error = "Synchronous CUDA processing requires an empty asynchronous pipeline";
    return false;
  }
  RawFrameBatchPtr borrowed(&raw_batch, [](const RawFrameBatch*) {});
  if (!submitBatch(borrowed, selected_record_index, error)) {
    return false;
  }
  RawFrameBatchPtr completed_batch;
  bool collected = false;
  if (!collectNext(true, completed_batch, processed_batch, collected, error) || !collected) {
    if (error.empty()) {
      error = "Synchronous CUDA processing did not collect its submitted batch";
    }
    return false;
  }
  error.clear();
  return true;
}

}  // namespace fmcw
