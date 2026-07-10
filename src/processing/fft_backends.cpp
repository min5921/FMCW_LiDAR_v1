#include "processing/fft_backends.h"

#include <algorithm>
#include <array>
#include <sstream>

#ifndef FMCW_HAS_FFTW
#define FMCW_HAS_FFTW 0
#endif

#ifndef FMCW_HAS_CUDA_FFT
#define FMCW_HAS_CUDA_FFT 0
#endif

#if FMCW_HAS_FFTW
#include <fftw3.h>
#endif

#if FMCW_HAS_CUDA_FFT
#include <cuda_runtime_api.h>
#include <cufft.h>
#endif

namespace fmcw {

struct FftwBackend::Impl {
  FftPlan plan;
#if FMCW_HAS_FFTW
  fftwf_plan handle = nullptr;
  std::vector<float> input;
  std::vector<std::array<float, 2>> output;
#endif
};

FftwBackend::FftwBackend() : impl_(std::make_unique<Impl>()) {}

FftwBackend::~FftwBackend() {
#if FMCW_HAS_FFTW
  if (impl_->handle != nullptr) {
    fftwf_destroy_plan(impl_->handle);
  }
#endif
}

bool FftwBackend::available() { return FMCW_HAS_FFTW != 0; }

std::string FftwBackend::name() const { return available() ? "FFTW3 single precision" : "FFTW3 unavailable"; }

FftBackendKind FftwBackend::kind() const { return FftBackendKind::Fftw; }

bool FftwBackend::prepare(const FftPlan& plan, std::string& error) {
  if (plan.length < 2U || plan.batch == 0U) {
    error = "FFTW plan length and batch must be positive";
    return false;
  }
#if FMCW_HAS_FFTW
  if (impl_->handle != nullptr && impl_->plan.length == plan.length && impl_->plan.batch == plan.batch) {
    error.clear();
    return true;
  }
  if (impl_->handle != nullptr) {
    fftwf_destroy_plan(impl_->handle);
    impl_->handle = nullptr;
  }
  impl_->input.assign(plan.length * plan.batch, 0.0F);
  impl_->output.resize((plan.length / 2U + 1U) * plan.batch);
  const int length = static_cast<int>(plan.length);
  const int batch = static_cast<int>(plan.batch);
  impl_->handle = fftwf_plan_many_dft_r2c(1, &length, batch, impl_->input.data(), nullptr, 1, length,
                                          reinterpret_cast<fftwf_complex*>(impl_->output.data()), nullptr, 1,
                                          length / 2 + 1, FFTW_ESTIMATE);
  if (impl_->handle == nullptr) {
    error = "FFTW failed to create an R2C plan";
    return false;
  }
  impl_->plan = plan;
  error.clear();
  return true;
#else
  static_cast<void>(plan);
  error = "FFTW backend was not compiled; set FFTW_ROOT or install fftw3f";
  return false;
#endif
}

bool FftwBackend::execute(const std::vector<float>& input, std::vector<std::complex<float>>& output,
                          std::string& error) {
#if FMCW_HAS_FFTW
  if (impl_->handle == nullptr || input.size() != impl_->input.size()) {
    error = "FFTW input does not match the prepared plan";
    return false;
  }
  std::copy(input.begin(), input.end(), impl_->input.begin());
  fftwf_execute(impl_->handle);
  output.resize(impl_->output.size());
  for (std::size_t index = 0; index < impl_->output.size(); ++index) {
    output[index] = {impl_->output[index][0], impl_->output[index][1]};
  }
  error.clear();
  return true;
#else
  static_cast<void>(input);
  output.clear();
  error = "FFTW backend was not compiled";
  return false;
#endif
}

struct CudaFftBackend::Impl {
  FftPlan plan;
#if FMCW_HAS_CUDA_FFT
  cufftHandle handle = 0;
  float* input = nullptr;
  cufftComplex* output = nullptr;
#endif
};

CudaFftBackend::CudaFftBackend() : impl_(std::make_unique<Impl>()) {}

CudaFftBackend::~CudaFftBackend() {
#if FMCW_HAS_CUDA_FFT
  if (impl_->handle != 0) {
    cufftDestroy(impl_->handle);
  }
  if (impl_->input != nullptr) {
    cudaFree(impl_->input);
  }
  if (impl_->output != nullptr) {
    cudaFree(impl_->output);
  }
#endif
}

bool CudaFftBackend::available() {
#if FMCW_HAS_CUDA_FFT
  int count = 0;
  return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
#else
  return false;
#endif
}

std::string CudaFftBackend::name() const {
  return FMCW_HAS_CUDA_FFT != 0 ? "CUDA cuFFT" : "CUDA cuFFT unavailable";
}

FftBackendKind CudaFftBackend::kind() const { return FftBackendKind::Cuda; }

bool CudaFftBackend::prepare(const FftPlan& plan, std::string& error) {
  if (plan.length < 2U || plan.batch == 0U) {
    error = "CUDA FFT plan length and batch must be positive";
    return false;
  }
#if FMCW_HAS_CUDA_FFT
  if (!available()) {
    error = "No CUDA device is available";
    return false;
  }
  if (impl_->handle != 0 && impl_->plan.length == plan.length && impl_->plan.batch == plan.batch) {
    error.clear();
    return true;
  }
  if (impl_->handle != 0) {
    cufftDestroy(impl_->handle);
    impl_->handle = 0;
  }
  if (impl_->input != nullptr) {
    cudaFree(impl_->input);
    impl_->input = nullptr;
  }
  if (impl_->output != nullptr) {
    cudaFree(impl_->output);
    impl_->output = nullptr;
  }
  if (cudaMalloc(reinterpret_cast<void**>(&impl_->input), plan.length * plan.batch * sizeof(float)) != cudaSuccess ||
      cudaMalloc(reinterpret_cast<void**>(&impl_->output),
                 (plan.length / 2U + 1U) * plan.batch * sizeof(cufftComplex)) != cudaSuccess) {
    error = "CUDA failed to allocate FFT buffers";
    return false;
  }
  const auto result = cufftPlan1d(&impl_->handle, static_cast<int>(plan.length), CUFFT_R2C,
                                  static_cast<int>(plan.batch));
  if (result != CUFFT_SUCCESS) {
    error = "cuFFT failed to create an R2C plan: " + std::to_string(static_cast<int>(result));
    return false;
  }
  impl_->plan = plan;
  error.clear();
  return true;
#else
  static_cast<void>(plan);
  error = "CUDA cuFFT backend was not compiled";
  return false;
#endif
}

bool CudaFftBackend::execute(const std::vector<float>& input, std::vector<std::complex<float>>& output,
                             std::string& error) {
#if FMCW_HAS_CUDA_FFT
  const auto input_count = impl_->plan.length * impl_->plan.batch;
  const auto output_count = (impl_->plan.length / 2U + 1U) * impl_->plan.batch;
  if (impl_->handle == 0 || input.size() != input_count) {
    error = "CUDA FFT input does not match the prepared plan";
    return false;
  }
  if (cudaMemcpy(impl_->input, input.data(), input_count * sizeof(float), cudaMemcpyHostToDevice) != cudaSuccess) {
    error = "CUDA failed to copy FFT input to the device";
    return false;
  }
  const auto fft_result = cufftExecR2C(impl_->handle, impl_->input, impl_->output);
  if (fft_result != CUFFT_SUCCESS) {
    error = "cuFFT execution failed: " + std::to_string(static_cast<int>(fft_result));
    return false;
  }
  std::vector<cufftComplex> host_output(output_count);
  if (cudaMemcpy(host_output.data(), impl_->output, output_count * sizeof(cufftComplex), cudaMemcpyDeviceToHost) !=
      cudaSuccess) {
    error = "CUDA failed to copy FFT output to the host";
    return false;
  }
  output.resize(output_count);
  for (std::size_t index = 0; index < output_count; ++index) {
    output[index] = {host_output[index].x, host_output[index].y};
  }
  error.clear();
  return true;
#else
  static_cast<void>(input);
  output.clear();
  error = "CUDA cuFFT backend was not compiled";
  return false;
#endif
}

std::unique_ptr<IFftBackend> createFftBackend(FftBackendKind kind) {
  if (kind == FftBackendKind::Cuda) {
    return std::make_unique<CudaFftBackend>();
  }
  return std::make_unique<FftwBackend>();
}

}  // namespace fmcw
