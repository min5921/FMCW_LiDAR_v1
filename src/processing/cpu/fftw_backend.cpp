#include "processing/fft_backends.h"

#ifndef FMCW_HAS_FFTW
#define FMCW_HAS_FFTW 0
#endif

#if FMCW_HAS_FFTW
#include <fftw3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#ifndef FMCW_HAS_OPENMP
#define FMCW_HAS_OPENMP 0
#endif

#if FMCW_HAS_OPENMP
#include <omp.h>
#endif

namespace fmcw {

namespace {

constexpr std::size_t kMaximumFftwLanes = 16U;

std::size_t availableLaneCount(std::size_t transform_count) {
#if FMCW_HAS_OPENMP
  const auto worker_count = static_cast<std::size_t>(std::max(omp_get_max_threads(), 1));
  return std::max<std::size_t>(
      1U, std::min({transform_count, worker_count, kMaximumFftwLanes}));
#else
  (void)transform_count;
  return 1U;
#endif
}

}  // namespace

struct FftwBackend::Impl {
  struct Lane {
    fftwf_plan handle = nullptr;
    std::vector<float> input;
    std::vector<std::array<float, 2>> output;
    std::size_t transform_offset = 0U;
    std::size_t batch = 0U;
  };

  void destroyPlans() {
    for (auto& lane : lanes) {
      if (lane.handle != nullptr) {
        fftwf_destroy_plan(lane.handle);
        lane.handle = nullptr;
      }
    }
    lanes.clear();
    plan = {};
  }

  FftPlan plan;
  std::vector<Lane> lanes;
};

FftwBackend::FftwBackend() : impl_(std::make_unique<Impl>()) {}

FftwBackend::~FftwBackend() {
  impl_->destroyPlans();
}

bool FftwBackend::available() { return true; }

std::string FftwBackend::name() const {
  return "FFTW3 single precision (" + std::to_string(impl_->lanes.size()) + " lanes)";
}

FftBackendKind FftwBackend::kind() const { return FftBackendKind::Fftw; }

bool FftwBackend::prepare(const FftPlan& plan, std::string& error) {
  if (plan.length < 2U || plan.batch == 0U) {
    error = "FFTW plan length and batch must be positive";
    return false;
  }
  if (!impl_->lanes.empty() && impl_->plan.length == plan.length &&
      impl_->plan.batch == plan.batch) {
    error.clear();
    return true;
  }
  impl_->destroyPlans();

  const int length = static_cast<int>(plan.length);
  const auto spectrum_length = plan.length / 2U + 1U;
  const auto lane_count = availableLaneCount(plan.batch);
  const auto transforms_per_lane = plan.batch / lane_count;
  const auto remainder = plan.batch % lane_count;
  std::size_t transform_offset = 0U;
  impl_->lanes.reserve(lane_count);
  for (std::size_t lane_index = 0; lane_index < lane_count; ++lane_index) {
    Impl::Lane lane;
    lane.transform_offset = transform_offset;
    lane.batch = transforms_per_lane + (lane_index < remainder ? 1U : 0U);
    lane.input.assign(plan.length * lane.batch, 0.0F);
    lane.output.resize(spectrum_length * lane.batch);
    const int lane_batch = static_cast<int>(lane.batch);
    lane.handle = fftwf_plan_many_dft_r2c(
        1, &length, lane_batch, lane.input.data(), nullptr, 1, length,
        reinterpret_cast<fftwf_complex*>(lane.output.data()), nullptr, 1,
        length / 2 + 1, FFTW_ESTIMATE);
    if (lane.handle == nullptr) {
      impl_->destroyPlans();
      error = "FFTW failed to create an R2C lane plan";
      return false;
    }
    transform_offset += lane.batch;
    impl_->lanes.push_back(std::move(lane));
  }
  impl_->plan = plan;
  error.clear();
  return true;
}

bool FftwBackend::execute(const std::vector<float>& input,
                          std::vector<std::complex<float>>& output, std::string& error) {
  if (impl_->lanes.empty() || input.size() != impl_->plan.length * impl_->plan.batch) {
    error = "FFTW input does not match the prepared plan";
    return false;
  }
  const auto spectrum_length = impl_->plan.length / 2U + 1U;
  output.resize(spectrum_length * impl_->plan.batch);
#if FMCW_HAS_OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (std::int64_t lane_number = 0;
       lane_number < static_cast<std::int64_t>(impl_->lanes.size()); ++lane_number) {
    auto& lane = impl_->lanes[static_cast<std::size_t>(lane_number)];
    const auto input_offset = lane.transform_offset * impl_->plan.length;
    const auto output_offset = lane.transform_offset * spectrum_length;
    std::copy_n(input.data() + input_offset, lane.input.size(), lane.input.data());
    fftwf_execute(lane.handle);
    for (std::size_t index = 0; index < lane.output.size(); ++index) {
      output[output_offset + index] = {lane.output[index][0], lane.output[index][1]};
    }
  }
  error.clear();
  return true;
}

}  // namespace fmcw
#endif
