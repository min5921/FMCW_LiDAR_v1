#include "processing/fft_backends.h"

#include <cuda_runtime.h>
#include <cufft.h>

#include <string>
#include <vector>

namespace fmcw {
namespace {

std::string cudaError(cudaError_t result, const char* operation) {
  return std::string(operation) + " failed: " + cudaGetErrorString(result);
}

std::string cufftError(cufftResult result, const char* operation) {
  return std::string(operation) + " failed with cuFFT code " +
      std::to_string(static_cast<int>(result));
}

}  // namespace

struct CudaFftBackend::Impl {
  FftPlan plan;
  cufftHandle handle = 0;
  cudaStream_t stream = nullptr;
  float* device_input = nullptr;
  cufftComplex* device_output = nullptr;

  void release() {
    if (handle != 0) {
      cufftDestroy(handle);
      handle = 0;
    }
    if (device_input != nullptr) {
      cudaFree(device_input);
      device_input = nullptr;
    }
    if (device_output != nullptr) {
      cudaFree(device_output);
      device_output = nullptr;
    }
    if (stream != nullptr) {
      cudaStreamDestroy(stream);
      stream = nullptr;
    }
    plan = {};
  }
};

CudaFftBackend::CudaFftBackend() : impl_(std::make_unique<Impl>()) {}

CudaFftBackend::~CudaFftBackend() { impl_->release(); }

bool CudaFftBackend::available() {
  int count = 0;
  return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

std::string CudaFftBackend::name() const { return "CUDA cuFFT"; }

FftBackendKind CudaFftBackend::kind() const { return FftBackendKind::Cuda; }

bool CudaFftBackend::prepare(const FftPlan& plan, std::string& error) {
  if (plan.length < 2U || plan.batch == 0U) {
    error = "CUDA FFT plan length and batch must be positive";
    return false;
  }
  if (!available()) {
    error = "No CUDA device is available";
    return false;
  }
  if (impl_->handle != 0 && impl_->plan.length == plan.length && impl_->plan.batch == plan.batch) {
    error.clear();
    return true;
  }

  impl_->release();
  auto cuda_result = cudaStreamCreateWithFlags(&impl_->stream, cudaStreamNonBlocking);
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "cudaStreamCreateWithFlags");
    impl_->release();
    return false;
  }

  const auto input_count = plan.length * plan.batch;
  const auto output_count = (plan.length / 2U + 1U) * plan.batch;
  cuda_result = cudaMalloc(reinterpret_cast<void**>(&impl_->device_input), input_count * sizeof(float));
  if (cuda_result == cudaSuccess) {
    cuda_result = cudaMalloc(reinterpret_cast<void**>(&impl_->device_output),
                             output_count * sizeof(cufftComplex));
  }
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "cudaMalloc FFT buffers");
    impl_->release();
    return false;
  }

  const auto fft_result = cufftPlan1d(&impl_->handle, static_cast<int>(plan.length), CUFFT_R2C,
                                      static_cast<int>(plan.batch));
  if (fft_result != CUFFT_SUCCESS) {
    error = cufftError(fft_result, "cufftPlan1d");
    impl_->release();
    return false;
  }
  const auto stream_result = cufftSetStream(impl_->handle, impl_->stream);
  if (stream_result != CUFFT_SUCCESS) {
    error = cufftError(stream_result, "cufftSetStream");
    impl_->release();
    return false;
  }

  impl_->plan = plan;
  error.clear();
  return true;
}

bool CudaFftBackend::execute(const std::vector<float>& input,
                             std::vector<std::complex<float>>& output, std::string& error) {
  const auto input_count = impl_->plan.length * impl_->plan.batch;
  const auto output_count = (impl_->plan.length / 2U + 1U) * impl_->plan.batch;
  if (impl_->handle == 0 || impl_->stream == nullptr || input.size() != input_count) {
    error = "CUDA FFT input does not match the prepared plan";
    return false;
  }

  auto cuda_result = cudaMemcpyAsync(impl_->device_input, input.data(), input_count * sizeof(float),
                                     cudaMemcpyHostToDevice, impl_->stream);
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "cudaMemcpyAsync host-to-device");
    return false;
  }
  const auto fft_result = cufftExecR2C(impl_->handle, impl_->device_input, impl_->device_output);
  if (fft_result != CUFFT_SUCCESS) {
    error = cufftError(fft_result, "cufftExecR2C");
    return false;
  }

  std::vector<cufftComplex> host_output(output_count);
  cuda_result = cudaMemcpyAsync(host_output.data(), impl_->device_output,
                                output_count * sizeof(cufftComplex), cudaMemcpyDeviceToHost,
                                impl_->stream);
  if (cuda_result == cudaSuccess) {
    cuda_result = cudaStreamSynchronize(impl_->stream);
  }
  if (cuda_result != cudaSuccess) {
    error = cudaError(cuda_result, "CUDA FFT result transfer");
    return false;
  }

  output.resize(output_count);
  for (std::size_t index = 0; index < output_count; ++index) {
    output[index] = {host_output[index].x, host_output[index].y};
  }
  error.clear();
  return true;
}

}  // namespace fmcw
