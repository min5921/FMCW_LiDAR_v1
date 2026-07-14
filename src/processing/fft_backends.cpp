#include "processing/fft_backends.h"

#ifndef FMCW_HAS_FFTW
#define FMCW_HAS_FFTW 0
#endif

#ifndef FMCW_HAS_CUDA_FFT
#define FMCW_HAS_CUDA_FFT 0
#endif

namespace fmcw {

#if !FMCW_HAS_FFTW
struct FftwBackend::Impl {};

FftwBackend::FftwBackend() : impl_(std::make_unique<Impl>()) {}
FftwBackend::~FftwBackend() = default;
bool FftwBackend::available() { return false; }
std::string FftwBackend::name() const { return "FFTW3 unavailable"; }
FftBackendKind FftwBackend::kind() const { return FftBackendKind::Fftw; }

bool FftwBackend::prepare(const FftPlan&, std::string& error) {
  error = "FFTW backend was not compiled; set FFTW_ROOT or install fftw3f";
  return false;
}

bool FftwBackend::execute(const std::vector<float>& input, std::vector<std::complex<float>>& output,
                          std::string& error) {
  static_cast<void>(input);
  output.clear();
  error = "FFTW backend was not compiled";
  return false;
}
#endif

#if !FMCW_HAS_CUDA_FFT
struct CudaFftBackend::Impl {};

CudaFftBackend::CudaFftBackend() : impl_(std::make_unique<Impl>()) {}
CudaFftBackend::~CudaFftBackend() = default;
bool CudaFftBackend::available() { return false; }
std::string CudaFftBackend::name() const { return "CUDA cuFFT unavailable"; }
FftBackendKind CudaFftBackend::kind() const { return FftBackendKind::Cuda; }

bool CudaFftBackend::prepare(const FftPlan&, std::string& error) {
  error = "CUDA cuFFT backend was not compiled; install the CUDA toolkit with nvcc and reconfigure CMake";
  return false;
}

bool CudaFftBackend::execute(const std::vector<float>& input, std::vector<std::complex<float>>& output,
                             std::string& error) {
  static_cast<void>(input);
  output.clear();
  error = "CUDA cuFFT backend was not compiled";
  return false;
}
#endif

std::unique_ptr<IFftBackend> createFftBackend(FftBackendKind kind) {
  if (kind == FftBackendKind::Cuda) {
    return std::make_unique<CudaFftBackend>();
  }
  return std::make_unique<FftwBackend>();
}

}  // namespace fmcw
