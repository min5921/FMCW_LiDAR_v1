#include "processing/fft_backends.h"

#include "core/realtime_thread.h"

#ifndef FMCW_HAS_FFTW
#define FMCW_HAS_FFTW 0
#endif

#if FMCW_HAS_FFTW
#include <fftw3.h>

#include <algorithm>
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
constexpr std::size_t kTransformsPerLaneCall = 8U;

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

#if FMCW_HAS_OPENMP
void prioritizeOpenMpWorkerThread() {
  if (omp_get_thread_num() != 0) {
    prioritizeCurrentRealtimeThread(RealtimeThreadPriority::High);
  }
}
#endif

}  // namespace

struct FftwBackend::Impl {
  struct Lane {
    fftwf_plan handle = nullptr;
    std::vector<float> input;
    std::vector<std::complex<float>> output;
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

#if FMCW_HAS_OPENMP
  omp_set_dynamic(0);
#endif
  const int length = static_cast<int>(plan.length);
  const auto spectrum_length = plan.length / 2U + 1U;
  const auto lane_count = availableLaneCount(plan.batch);
  const auto lane_batch = std::min(kTransformsPerLaneCall, plan.batch);
  impl_->lanes.reserve(lane_count);
  for (std::size_t lane_index = 0; lane_index < lane_count; ++lane_index) {
    Impl::Lane lane;
    lane.batch = lane_batch;
    lane.input.assign(plan.length * lane.batch, 0.0F);
    lane.output.resize(spectrum_length * lane.batch);
    const int lane_plan_batch = static_cast<int>(lane.batch);
    lane.handle = fftwf_plan_many_dft_r2c(
        1, &length, lane_plan_batch, lane.input.data(), nullptr, 1, length,
        reinterpret_cast<fftwf_complex*>(lane.output.data()), nullptr, 1,
        length / 2 + 1, FFTW_ESTIMATE);
    if (lane.handle == nullptr) {
      impl_->destroyPlans();
      error = "FFTW failed to create an R2C lane plan";
      return false;
    }
    impl_->lanes.push_back(std::move(lane));
  }
  impl_->plan = plan;
#if FMCW_HAS_OPENMP
  const auto lane_thread_count = static_cast<int>(impl_->lanes.size());
#pragma omp parallel num_threads(lane_thread_count)
  {
    prioritizeOpenMpWorkerThread();
  }
#endif
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
  const auto lane_thread_count = static_cast<int>(impl_->lanes.size());
#pragma omp parallel num_threads(lane_thread_count)
  {
    prioritizeOpenMpWorkerThread();
#pragma omp for schedule(static)
#endif
    for (std::int64_t lane_number = 0;
         lane_number < static_cast<std::int64_t>(impl_->lanes.size()); ++lane_number) {
      auto& lane = impl_->lanes[static_cast<std::size_t>(lane_number)];
      const auto lane_index = static_cast<std::size_t>(lane_number);
      const auto transform_stride = impl_->lanes.size() * lane.batch;
      for (std::size_t transform_offset = lane_index * lane.batch;
           transform_offset < impl_->plan.batch;
           transform_offset += transform_stride) {
        const auto transform_count = std::min(lane.batch,
                                              impl_->plan.batch - transform_offset);
        auto* direct_input = const_cast<float*>(
            input.data() + transform_offset * impl_->plan.length);
        auto* direct_output = output.data() + transform_offset * spectrum_length;
        const bool full_lane_batch = transform_count == lane.batch;
        const bool compatible_alignment =
            fftwf_alignment_of(direct_input) == fftwf_alignment_of(lane.input.data()) &&
            fftwf_alignment_of(reinterpret_cast<float*>(direct_output)) ==
                fftwf_alignment_of(reinterpret_cast<float*>(lane.output.data()));
        if (full_lane_batch && compatible_alignment) {
          fftwf_execute_dft_r2c(
              lane.handle, direct_input,
              reinterpret_cast<fftwf_complex*>(direct_output));
          continue;
        }

        const auto input_count = transform_count * impl_->plan.length;
        std::copy_n(direct_input, input_count, lane.input.data());
        if (!full_lane_batch) {
          std::fill(lane.input.begin() + static_cast<std::ptrdiff_t>(input_count),
                    lane.input.end(), 0.0F);
        }
        fftwf_execute(lane.handle);
        const auto output_count = transform_count * spectrum_length;
        std::copy_n(lane.output.data(), output_count, direct_output);
      }
    }
#if FMCW_HAS_OPENMP
  }
#endif
  error.clear();
  return true;
}

}  // namespace fmcw
#endif
