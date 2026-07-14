#include "processing/fft_backends.h"

#ifndef FMCW_HAS_FFTW
#define FMCW_HAS_FFTW 0
#endif

#if FMCW_HAS_FFTW
#include <fftw3.h>

#include <algorithm>
#include <array>

namespace fmcw {

struct FftwBackend::Impl {
  FftPlan plan;
  fftwf_plan handle = nullptr;
  std::vector<float> input;
  std::vector<std::array<float, 2>> output;
};

FftwBackend::FftwBackend() : impl_(std::make_unique<Impl>()) {}

FftwBackend::~FftwBackend() {
  if (impl_->handle != nullptr) {
    fftwf_destroy_plan(impl_->handle);
  }
}

bool FftwBackend::available() { return true; }

std::string FftwBackend::name() const { return "FFTW3 single precision"; }

FftBackendKind FftwBackend::kind() const { return FftBackendKind::Fftw; }

bool FftwBackend::prepare(const FftPlan& plan, std::string& error) {
  if (plan.length < 2U || plan.batch == 0U) {
    error = "FFTW plan length and batch must be positive";
    return false;
  }
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
}

bool FftwBackend::execute(const std::vector<float>& input,
                          std::vector<std::complex<float>>& output, std::string& error) {
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
}

}  // namespace fmcw
#endif
