#if defined(FMCW_TARGET_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "drivers/alazar/alazar_digitizer.h"

#include "core/config_validation.h"
#include "core/digitizer_capabilities.h"
#include "core/raw_frame_batch_pool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#ifndef FMCW_HAS_ALAZAR_SDK
#define FMCW_HAS_ALAZAR_SDK 0
#endif

#if FMCW_HAS_ALAZAR_SDK
#include "AlazarError.h"
#include "AlazarApi.h"
#include "AlazarCmd.h"
#endif

namespace fmcw {
namespace {

std::uint64_t nowNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

#if FMCW_HAS_ALAZAR_SDK

static_assert(static_cast<std::uint32_t>(ATS9350) == 14U);
static_assert(static_cast<std::uint32_t>(ATS9351) == 18U);
static_assert(static_cast<std::uint32_t>(ATS9360) == 25U);
static_assert(static_cast<std::uint32_t>(ATS9373) == 29U);
static_assert(static_cast<std::uint32_t>(ATS9120) == 32U);
static_assert(static_cast<std::uint32_t>(ATS9371) == 33U);
static_assert(static_cast<std::uint32_t>(ATS9130) == 34U);
static_assert(static_cast<std::uint32_t>(ATS9352) == 35U);
static_assert(static_cast<std::uint32_t>(ATS9353) == 44U);
static_assert(static_cast<std::uint32_t>(ATS9364) == 53U);
static_assert(static_cast<std::uint32_t>(ATS9362) == 58U);

bool check(RETURN_CODE code, const char* operation, std::string& error) {
  if (code == ApiSuccess) {
    return true;
  }
  error = std::string(operation) + " failed: " + AlazarErrorToText(code);
  return false;
}

U32 channelMask(DigitizerChannel channel) {
  return channel == DigitizerChannel::A ? CHANNEL_A : CHANNEL_B;
}

U32 couplingId(Coupling coupling) {
  return coupling == Coupling::Ac ? AC_COUPLING : DC_COUPLING;
}

U32 triggerSlopeId(TriggerSlope slope) {
  return slope == TriggerSlope::Rising ? TRIGGER_SLOPE_POSITIVE : TRIGGER_SLOPE_NEGATIVE;
}

U32 sampleRateId(double sample_rate_hz) {
  switch (static_cast<std::uint64_t>(std::llround(sample_rate_hz))) {
    case 1000ULL: return SAMPLE_RATE_1KSPS;
    case 2000ULL: return SAMPLE_RATE_2KSPS;
    case 5000ULL: return SAMPLE_RATE_5KSPS;
    case 10000ULL: return SAMPLE_RATE_10KSPS;
    case 20000ULL: return SAMPLE_RATE_20KSPS;
    case 50000ULL: return SAMPLE_RATE_50KSPS;
    case 100000ULL: return SAMPLE_RATE_100KSPS;
    case 200000ULL: return SAMPLE_RATE_200KSPS;
    case 500000ULL: return SAMPLE_RATE_500KSPS;
    case 1000000ULL: return SAMPLE_RATE_1MSPS;
    case 2000000ULL: return SAMPLE_RATE_2MSPS;
    case 5000000ULL: return SAMPLE_RATE_5MSPS;
    case 10000000ULL: return SAMPLE_RATE_10MSPS;
    case 20000000ULL: return SAMPLE_RATE_20MSPS;
    case 25000000ULL: return SAMPLE_RATE_25MSPS;
    case 50000000ULL: return SAMPLE_RATE_50MSPS;
    case 100000000ULL: return SAMPLE_RATE_100MSPS;
    case 125000000ULL: return SAMPLE_RATE_125MSPS;
    case 200000000ULL: return SAMPLE_RATE_200MSPS;
    case 250000000ULL: return SAMPLE_RATE_250MSPS;
    case 500000000ULL: return SAMPLE_RATE_500MSPS;
    case 750000000ULL: return SAMPLE_RATE_750MSPS;
    case 800000000ULL: return SAMPLE_RATE_800MSPS;
    case 1000000000ULL: return SAMPLE_RATE_1000MSPS;
    case 1200000000ULL: return SAMPLE_RATE_1200MSPS;
    case 1500000000ULL: return SAMPLE_RATE_1500MSPS;
    case 1800000000ULL: return SAMPLE_RATE_1800MSPS;
    case 2000000000ULL: return SAMPLE_RATE_2000MSPS;
    case 2400000000ULL: return SAMPLE_RATE_2400MSPS;
    case 3000000000ULL: return SAMPLE_RATE_3000MSPS;
    case 3600000000ULL: return SAMPLE_RATE_3600MSPS;
    case 4000000000ULL: return SAMPLE_RATE_4000MSPS;
    default: return 0U;
  }
}

U32 inputRangeId(double input_range_volts) {
  switch (static_cast<std::uint32_t>(std::llround(input_range_volts * 1000.0))) {
    case 40U: return INPUT_RANGE_PM_40_MV;
    case 50U: return INPUT_RANGE_PM_50_MV;
    case 80U: return INPUT_RANGE_PM_80_MV;
    case 100U: return INPUT_RANGE_PM_100_MV;
    case 200U: return INPUT_RANGE_PM_200_MV;
    case 400U: return INPUT_RANGE_PM_400_MV;
    case 500U: return INPUT_RANGE_PM_500_MV;
    case 800U: return INPUT_RANGE_PM_800_MV;
    case 1000U: return INPUT_RANGE_PM_1_V;
    case 2000U: return INPUT_RANGE_PM_2_V;
    case 4000U: return INPUT_RANGE_PM_4_V;
    default: return 0U;
  }
}

U32 externalTriggerRangeId(AlazarExternalTriggerRange range) {
  return range == AlazarExternalTriggerRange::FiveVolts ? ETR_5V : ETR_TTL;
}

struct AlazarDmaBuffer {
  AlazarDmaBuffer(HANDLE source_board, U16* source_data)
      : board(source_board), data(source_data) {}
  AlazarDmaBuffer(const AlazarDmaBuffer&) = delete;
  AlazarDmaBuffer& operator=(const AlazarDmaBuffer&) = delete;

  HANDLE board = nullptr;
  U16* data = nullptr;

  ~AlazarDmaBuffer() {
    if (board != nullptr && data != nullptr) {
      AlazarFreeBufferU16(board, data);
    }
  }
};

struct AlazarDmaLeaseState {
  std::mutex mutex;
  std::condition_variable condition;
  std::deque<U32> released_indices;
  std::size_t outstanding = 0U;
  bool repost_enabled = false;
};

struct AlazarDmaLease {
  AlazarDmaLease(std::shared_ptr<AlazarDmaBuffer> source_allocation,
                 std::shared_ptr<AlazarDmaLeaseState> source_state,
                 U32 source_buffer_index)
      : allocation(std::move(source_allocation)),
        state(std::move(source_state)),
        buffer_index(source_buffer_index) {}
  AlazarDmaLease(const AlazarDmaLease&) = delete;
  AlazarDmaLease& operator=(const AlazarDmaLease&) = delete;

  std::shared_ptr<AlazarDmaBuffer> allocation;
  std::shared_ptr<AlazarDmaLeaseState> state;
  U32 buffer_index = 0U;

  ~AlazarDmaLease() {
    if (!state) {
      return;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->outstanding > 0U) {
      --state->outstanding;
    }
    if (state->repost_enabled) {
      state->released_indices.push_back(buffer_index);
    }
    state->condition.notify_all();
  }
};

#endif

}  // namespace

struct AlazarDigitizer::Impl {
#if FMCW_HAS_ALAZAR_SDK
  HANDLE board = nullptr;
  const DigitizerBoardCapabilities* capabilities = nullptr;
  U8 bits_per_sample = 0;
  std::vector<std::shared_ptr<AlazarDmaBuffer>> buffers;
  std::vector<RawFrameMetadata> record_metadata_templates;
  std::deque<U32> posted_indices;
  std::shared_ptr<AlazarDmaLeaseState> lease_state;
  U32 bytes_per_buffer = 0;
  U32 records_per_buffer = 0;
  RawFrameBatchPool batch_pool;
  MutableRawFrameBatchPtr compatibility_batch;
  std::size_t compatibility_record_index = 0;
  bool async_prepared = false;
  std::uint64_t next_frame_id = 1;
  std::uint64_t current_buffer_timestamp_ns = 0;
  std::uint64_t previous_buffer_timestamp_ns = 0;
  double buffer_period_ema_ms = 0.0;
  std::condition_variable api_wait_condition;
  bool api_wait_active = false;
#endif
};

AlazarDigitizer::AlazarDigitizer() : impl_(std::make_unique<Impl>()) {
  telemetry_.device.detail = sdkAvailable() ? "Alazar SDK available" : "Alazar SDK not linked";
}

AlazarDigitizer::~AlazarDigitizer() { disconnect(); }

bool AlazarDigitizer::sdkAvailable() { return FMCW_HAS_ALAZAR_SDK != 0; }

std::string AlazarDigitizer::name() const { return "AlazarTech ATS AutoDMA digitizer"; }

DigitizerTelemetry AlazarDigitizer::telemetry() const {
  std::lock_guard<std::mutex> lock(telemetry_mutex_);
  return telemetry_;
}

bool AlazarDigitizer::configure(const SystemConfig& config, std::string& error) {
  const auto validation = ConfigValidator::validate(config);
  if (validation.hasErrors()) {
    error = "Alazar digitizer rejected invalid system configuration";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    if (telemetry_.device.running) {
      error = "Cannot configure Alazar digitizer while acquisition is running";
      return false;
    }
  }
  const auto* capabilities = findDigitizerBoardCapabilities(config.digitizer.board_profile);
  if (capabilities == nullptr || !supportsSampleRate(*capabilities, config.digitizer.sample_rate_hz) ||
      !supportsInputRange(*capabilities, config.digitizer.input_range_volts) ||
      !supportsImpedance(*capabilities, config.digitizer.impedance_ohms) ||
      capabilities->bits_per_sample != 12U || !capabilities->aux_trigger_enable_supported ||
      config.digitizer.trigger_source != TriggerSource::External ||
      config.digitizer.coupling != Coupling::Dc) {
    error = "Alazar settings are outside the selected board capability profile";
    return false;
  }
#if FMCW_HAS_ALAZAR_SDK
  if (impl_->board != nullptr &&
      (impl_->capabilities == nullptr ||
       impl_->capabilities->profile_id != capabilities->profile_id)) {
    error = "Selected board model does not match the connected Alazar board";
    return false;
  }
#endif
  config_ = config;
  config_.digitizer.fifo_only_streaming = capabilities->fifo_only_streaming_supported;
  configured_ = true;
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.detail =
        "Alazar configuration cached for channel " + toString(config_.digitizer.channel);
  }
#if FMCW_HAS_ALAZAR_SDK
  if (impl_->board != nullptr && !configureBoard(error)) {
    return false;
  }
#endif
  error.clear();
  return true;
}

bool AlazarDigitizer::connect(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!configured_) {
    error = "Configure the Alazar digitizer before connecting";
    return false;
  }
#if FMCW_HAS_ALAZAR_SDK
  impl_->board = AlazarGetBoardBySystemID(kAlazarSystemId, kAlazarBoardId);
  if (impl_->board == nullptr) {
    error = "AlazarGetBoardBySystemID returned no board for System 1 / Board 1";
    return false;
  }
  const auto board_kind = static_cast<std::uint32_t>(AlazarGetBoardKind(impl_->board));
  const auto* detected_capabilities =
      findDigitizerBoardCapabilitiesBySdkBoardKind(board_kind);
  if (detected_capabilities == nullptr) {
    error = "Detected Alazar board kind " + std::to_string(board_kind) +
            " is not in the supported 12-bit AUX trigger-enable model list";
    impl_->board = nullptr;
    return false;
  }
  const auto* selected_capabilities =
      findDigitizerBoardCapabilities(config_.digitizer.board_profile);
  if (selected_capabilities == nullptr ||
      selected_capabilities->sdk_board_kind != board_kind) {
    error = "Selected " +
            (selected_capabilities == nullptr ? config_.digitizer.board_profile
                                              : selected_capabilities->display_name) +
            " but detected " + detected_capabilities->display_name +
            " at System 1 / Board 1";
    impl_->board = nullptr;
    return false;
  }
  impl_->capabilities = detected_capabilities;
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.connected = true;
  }
  if (!configureBoard(error)) {
    impl_->board = nullptr;
    impl_->capabilities = nullptr;
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.connected = false;
    return false;
  }
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.ready = true;
    telemetry_.device.detail = impl_->capabilities->display_name +
        " System 1 / Board 1 connected and configured";
  }
  error.clear();
  return true;
#else
  error = "Alazar SDK support is not built. Set ALAZAR_SDK_ROOT and reconfigure CMake";
  return false;
#endif
}

void AlazarDigitizer::disconnect() {
  std::unique_lock<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
  std::string ignored;
  if (!abortAsyncReadLocked(lock, ignored)) {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.detail = ignored;
    return;
  }
  releaseBuffers();
  impl_->board = nullptr;
  impl_->capabilities = nullptr;
  impl_->bits_per_sample = 0U;
#endif
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device = {};
    telemetry_.device.detail = sdkAvailable() ? "Alazar disconnected" : "Alazar SDK not linked";
  }
}

bool AlazarDigitizer::start(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    if (!configured_ || impl_->board == nullptr || !telemetry_.device.ready ||
        telemetry_.device.running) {
      error = "Alazar digitizer is not ready or is already running";
      return false;
    }
  }
  releaseBuffers();
  impl_->records_per_buffer = config_.digitizer.acquisition_mode == AcquisitionMode::Finite ?
      1U : config_.digitizer.records_per_buffer;
  impl_->record_metadata_templates.assign(impl_->records_per_buffer, {});
  for (U32 record_index = 0U; record_index < impl_->records_per_buffer; ++record_index) {
    auto& metadata = impl_->record_metadata_templates[record_index];
    metadata.frame_kind = FrameKind::FullChirpPeriod;
    metadata.record_index_in_buffer = record_index;
    metadata.records_in_buffer = impl_->records_per_buffer;
    metadata.trigger.valid = true;
    metadata.channel = config_.digitizer.channel;
    metadata.sample_format = SampleFormat::UnsignedOffsetBinary12LeftAligned;
    metadata.byte_order = ByteOrder::LittleEndian;
    metadata.sample_rate_hz = config_.digitizer.sample_rate_hz;
    metadata.record_length = config_.digitizer.sample_point;
    metadata.pre_trigger_samples = config_.digitizer.pre_trigger_samples;
    metadata.post_trigger_samples = config_.digitizer.post_trigger_samples;
    metadata.up_segment = config_.chirp_segmentation.up_segment;
    metadata.down_segment = config_.chirp_segmentation.down_segment;
  }
  const std::uint64_t bytes = static_cast<std::uint64_t>(config_.digitizer.sample_point) *
                              impl_->records_per_buffer * sizeof(U16);
  if (bytes == 0 || bytes > std::numeric_limits<U32>::max()) {
    error = "Alazar DMA buffer size exceeds the ATS API limit";
    return false;
  }
  impl_->bytes_per_buffer = static_cast<U32>(bytes);
  impl_->buffers.clear();
  impl_->buffers.reserve(config_.digitizer.dma_buffer_count);
  for (std::uint32_t index = 0U; index < config_.digitizer.dma_buffer_count; ++index) {
    auto* data = AlazarAllocBufferU16(impl_->board, impl_->bytes_per_buffer);
    if (data == nullptr) {
      error = "AlazarAllocBufferU16 failed";
      releaseBuffers();
      return false;
    }
    impl_->buffers.push_back(std::make_shared<AlazarDmaBuffer>(impl_->board, data));
  }
  if (!check(AlazarSetRecordSize(impl_->board, config_.digitizer.pre_trigger_samples,
                                 config_.digitizer.post_trigger_samples), "AlazarSetRecordSize", error)) {
    releaseBuffers();
    return false;
  }
  const U32 records_per_acquisition = config_.digitizer.acquisition_mode == AcquisitionMode::Finite ?
      config_.digitizer.finite_frame_count : 0x7FFFFFFFU;
  U32 flags = ADMA_EXTERNAL_STARTCAPTURE | ADMA_NPT;
  if (impl_->capabilities != nullptr &&
      impl_->capabilities->fifo_only_streaming_supported) {
    flags |= ADMA_FIFO_ONLY_STREAMING;
  }
  if (!check(AlazarBeforeAsyncRead(impl_->board, channelMask(config_.digitizer.channel),
                                   -static_cast<long>(config_.digitizer.pre_trigger_samples),
                                   config_.digitizer.sample_point, impl_->records_per_buffer,
                                   records_per_acquisition, flags), "AlazarBeforeAsyncRead", error)) {
    releaseBuffers();
    return false;
  }
  impl_->async_prepared = true;
  impl_->lease_state = std::make_shared<AlazarDmaLeaseState>();
  impl_->lease_state->repost_enabled = true;
  impl_->posted_indices.clear();
  for (U32 index = 0U; index < static_cast<U32>(impl_->buffers.size()); ++index) {
    if (!check(AlazarPostAsyncBuffer(impl_->board, impl_->buffers[index]->data,
                                     impl_->bytes_per_buffer),
               "AlazarPostAsyncBuffer", error)) {
      AlazarAbortAsyncRead(impl_->board);
      impl_->async_prepared = false;
      releaseBuffers();
      return false;
    }
    impl_->posted_indices.push_back(index);
  }
  if (!check(AlazarStartCapture(impl_->board), "AlazarStartCapture", error)) {
    AlazarAbortAsyncRead(impl_->board);
    impl_->async_prepared = false;
    releaseBuffers();
    return false;
  }
  impl_->compatibility_batch.reset();
  impl_->compatibility_record_index = 0;
  impl_->next_frame_id = 1;
  impl_->current_buffer_timestamp_ns = 0;
  impl_->previous_buffer_timestamp_ns = 0;
  impl_->buffer_period_ema_ms = 0.0;
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.frames_received = 0;
    telemetry_.dma_buffers_received = 0;
    telemetry_.dma_buffer_drops = 0;
    telemetry_.trigger_misses = 0;
    telemetry_.dma_buffer_rate_hz = 0.0;
    telemetry_.dma_buffer_period_ms = 0.0;
    telemetry_.device.running = true;
    telemetry_.device.detail = "Alazar NPT AutoDMA acquisition active";
  }
  error.clear();
  return true;
#else
  error = "Alazar SDK support is not built";
  return false;
#endif
}

FrameWaitResult AlazarDigitizer::waitForBatch(MutableRawFrameBatchPtr& batch,
                                              std::chrono::milliseconds timeout,
                                              std::string& error) {
  std::unique_lock<std::mutex> lock(mutex_);
  batch.reset();
#if FMCW_HAS_ALAZAR_SDK
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    if (!telemetry_.device.running || !impl_->async_prepared) {
      error.clear();
      return FrameWaitResult::Stopped;
    }
    if (config_.digitizer.acquisition_mode == AcquisitionMode::Finite &&
        telemetry_.frames_received >= config_.digitizer.finite_frame_count) {
      telemetry_.device.running = false;
      error.clear();
      return FrameWaitResult::Stopped;
    }
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  const auto repostReleasedBuffers = [&]() -> bool {
    std::deque<U32> released;
    {
      std::lock_guard<std::mutex> lease_lock(impl_->lease_state->mutex);
      released.swap(impl_->lease_state->released_indices);
    }
    while (!released.empty()) {
      const auto index = released.front();
      released.pop_front();
      if (index >= impl_->buffers.size() ||
          !check(AlazarPostAsyncBuffer(impl_->board, impl_->buffers[index]->data,
                                       impl_->bytes_per_buffer),
                 "AlazarPostAsyncBuffer(repost)", error)) {
        return false;
      }
      impl_->posted_indices.push_back(index);
    }
    return true;
  };

  if (!repostReleasedBuffers()) {
    return FrameWaitResult::Error;
  }
  while (impl_->posted_indices.empty()) {
    if (!repostReleasedBuffers()) {
      return FrameWaitResult::Error;
    }
    if (!impl_->posted_indices.empty()) {
      break;
    }
    std::unique_lock<std::mutex> lease_lock(impl_->lease_state->mutex);
    if (!impl_->lease_state->repost_enabled) {
      error.clear();
      return FrameWaitResult::Stopped;
    }
    if (!impl_->lease_state->condition.wait_until(lease_lock, deadline, [this] {
          return !impl_->lease_state->released_indices.empty() ||
              !impl_->lease_state->repost_enabled;
        })) {
      error.clear();
      return FrameWaitResult::Timeout;
    }
  }

  const auto buffer_index = impl_->posted_indices.front();
  auto* buffer = impl_->buffers[buffer_index]->data;
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::max(deadline - std::chrono::steady_clock::now(),
               std::chrono::steady_clock::duration::zero()));
  const auto wait_ms = static_cast<U32>(std::clamp<std::int64_t>(
      remaining.count(), 0, std::numeric_limits<U32>::max()));

  impl_->api_wait_active = true;
  lock.unlock();
  const auto result = AlazarWaitAsyncBufferComplete(impl_->board, buffer, wait_ms);
  const auto acquisition_wakeup_timestamp_ns = nowNs();
  lock.lock();
  impl_->api_wait_active = false;
  impl_->api_wait_condition.notify_all();
  if (!impl_->async_prepared) {
    error.clear();
    return FrameWaitResult::Stopped;
  }
  if (result == ApiWaitTimeout) {
    error.clear();
    return FrameWaitResult::Timeout;
  }
  if (result == ApiBufferOverflow) {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    ++telemetry_.dma_buffer_drops;
  }
  if (!check(result, "AlazarWaitAsyncBufferComplete", error)) {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.detail = error;
    return FrameWaitResult::Error;
  }
  impl_->posted_indices.pop_front();

  impl_->current_buffer_timestamp_ns = acquisition_wakeup_timestamp_ns;
  if (impl_->previous_buffer_timestamp_ns != 0U &&
      impl_->current_buffer_timestamp_ns > impl_->previous_buffer_timestamp_ns) {
    const auto period_ms = static_cast<double>(impl_->current_buffer_timestamp_ns -
        impl_->previous_buffer_timestamp_ns) * 1.0e-6;
    constexpr double kTelemetryAlpha = 0.2;
    impl_->buffer_period_ema_ms = impl_->buffer_period_ema_ms > 0.0
        ? kTelemetryAlpha * period_ms + (1.0 - kTelemetryAlpha) * impl_->buffer_period_ema_ms
        : period_ms;
  }
  impl_->previous_buffer_timestamp_ns = impl_->current_buffer_timestamp_ns;

  std::uint64_t batch_sequence = 0U;
  std::uint64_t dropped_buffer_count = 0U;
  std::uint64_t missed_trigger_count = 0U;
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    if (impl_->buffer_period_ema_ms > 0.0) {
      telemetry_.dma_buffer_period_ms = impl_->buffer_period_ema_ms;
      telemetry_.dma_buffer_rate_hz = 1000.0 / impl_->buffer_period_ema_ms;
    }
    batch_sequence = telemetry_.dma_buffers_received;
    dropped_buffer_count = telemetry_.dma_buffer_drops;
    missed_trigger_count = telemetry_.trigger_misses;
  }

  auto mutable_batch = impl_->batch_pool.acquire();
  const auto sample_count = static_cast<std::size_t>(impl_->records_per_buffer) *
      config_.digitizer.sample_point;
  mutable_batch->contiguous_samples.setView(reinterpret_cast<std::int16_t*>(buffer), sample_count);
  {
    std::lock_guard<std::mutex> lease_lock(impl_->lease_state->mutex);
    ++impl_->lease_state->outstanding;
  }
  mutable_batch->sample_owner = std::make_shared<AlazarDmaLease>(
      impl_->buffers[buffer_index], impl_->lease_state, buffer_index);
  mutable_batch->records.resize(impl_->records_per_buffer);
  for (U32 record_index = 0; record_index < impl_->records_per_buffer; ++record_index) {
    auto& frame = mutable_batch->records[record_index];
    frame.metadata = impl_->record_metadata_templates[record_index];
    const auto offset = static_cast<std::size_t>(record_index) * config_.digitizer.sample_point;
    frame.samples.setView(mutable_batch->contiguous_samples.data() + offset,
                          config_.digitizer.sample_point);

    const auto frame_id = impl_->next_frame_id++;
    frame.metadata.frame_id = frame_id;
    frame.metadata.dma_buffer_sequence = batch_sequence;
    frame.metadata.host_timestamp_ns = impl_->current_buffer_timestamp_ns;
    frame.metadata.trigger.sequence = frame_id;
    frame.metadata.trigger.timestamp_ns = frame.metadata.host_timestamp_ns;
  }
  mutable_batch->metadata.sequence = batch_sequence;
  mutable_batch->metadata.completion_timestamp_ns = impl_->current_buffer_timestamp_ns;
  mutable_batch->metadata.acquisition_wakeup_timestamp_ns =
      acquisition_wakeup_timestamp_ns;
  mutable_batch->metadata.ownership_ready_timestamp_ns = nowNs();
  mutable_batch->metadata.record_count = impl_->records_per_buffer;
  mutable_batch->metadata.record_length = config_.digitizer.sample_point;
  mutable_batch->metadata.dropped_buffer_count = dropped_buffer_count;
  mutable_batch->metadata.missed_trigger_count = missed_trigger_count;

  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.frames_received += impl_->records_per_buffer;
    ++telemetry_.dma_buffers_received;
  }
  batch = std::move(mutable_batch);
  error.clear();
  return FrameWaitResult::FrameReady;
#else
  static_cast<void>(timeout);
  error = "Alazar SDK support is not built";
  return FrameWaitResult::Error;
#endif
}

FrameWaitResult AlazarDigitizer::waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout,
                                              std::string& error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
    if (impl_->compatibility_batch &&
        impl_->compatibility_record_index < impl_->compatibility_batch->records.size()) {
      frame = impl_->compatibility_batch->records[impl_->compatibility_record_index++];
      if (impl_->compatibility_record_index >= impl_->compatibility_batch->records.size()) {
        impl_->compatibility_batch.reset();
      }
      error.clear();
      return FrameWaitResult::FrameReady;
    }
#endif
  }

  MutableRawFrameBatchPtr batch;
  const auto result = waitForBatch(batch, timeout, error);
  if (result != FrameWaitResult::FrameReady || !batch || batch->records.empty()) {
    return result;
  }
  std::lock_guard<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
  impl_->compatibility_batch = std::move(batch);
  impl_->compatibility_record_index = 1;
  frame = impl_->compatibility_batch->records.front();
  if (impl_->compatibility_record_index >= impl_->compatibility_batch->records.size()) {
    impl_->compatibility_batch.reset();
  }
  return FrameWaitResult::FrameReady;
#else
  static_cast<void>(frame);
  return FrameWaitResult::Error;
#endif
}

bool AlazarDigitizer::abort(std::string& error) {
  std::unique_lock<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
  if (!abortAsyncReadLocked(lock, error)) {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.detail = error;
    return false;
  }
#endif
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.running = false;
    telemetry_.device.detail = "Alazar acquisition aborted";
  }
  error.clear();
  return true;
}

bool AlazarDigitizer::stop(std::string& error) {
  std::unique_lock<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
  if (!abortAsyncReadLocked(lock, error)) {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.detail = error;
    return false;
  }
  releaseBuffers();
#endif
  {
    std::lock_guard<std::mutex> telemetry_lock(telemetry_mutex_);
    telemetry_.device.running = false;
    telemetry_.device.ready = telemetry_.device.connected;
    telemetry_.device.detail = telemetry_.device.connected ? "Alazar ready" : "Alazar disconnected";
  }
  error.clear();
  return true;
}

bool AlazarDigitizer::abortAsyncReadLocked(std::unique_lock<std::mutex>& lock,
                                           std::string& error) {
#if FMCW_HAS_ALAZAR_SDK
  if (impl_->async_prepared && impl_->board != nullptr) {
    if (!check(AlazarAbortAsyncRead(impl_->board), "AlazarAbortAsyncRead", error)) {
      return false;
    }
    impl_->async_prepared = false;
  }
  if (impl_->lease_state) {
    std::lock_guard<std::mutex> lease_lock(impl_->lease_state->mutex);
    impl_->lease_state->repost_enabled = false;
    impl_->lease_state->condition.notify_all();
  }
  impl_->api_wait_condition.wait(lock, [this] { return !impl_->api_wait_active; });
#else
  static_cast<void>(lock);
#endif
  error.clear();
  return true;
}

bool AlazarDigitizer::configureBoard(std::string& error) {
#if FMCW_HAS_ALAZAR_SDK
  if (impl_->capabilities == nullptr) {
    error = "No supported Alazar board model has been detected";
    return false;
  }
  const auto sample_rate_id = sampleRateId(config_.digitizer.sample_rate_hz);
  if (sample_rate_id == 0U) {
    error = "Unsupported " + impl_->capabilities->display_name +
            " internal-clock sample rate";
    return false;
  }
  const auto input_range_id = inputRangeId(config_.digitizer.input_range_volts);
  if (input_range_id == 0U) {
    error = "Unsupported " + impl_->capabilities->display_name +
            " input range";
    return false;
  }
  if (!check(AlazarSetCaptureClock(impl_->board, INTERNAL_CLOCK, sample_rate_id,
                                   CLOCK_EDGE_RISING, 0), "AlazarSetCaptureClock", error) ||
      !check(AlazarInputControlEx(impl_->board, channelMask(config_.digitizer.channel),
                                  couplingId(config_.digitizer.coupling), input_range_id,
                                  IMPEDANCE_50_OHM), "AlazarInputControlEx", error) ||
      !check(AlazarSetTriggerOperation(impl_->board, TRIG_ENGINE_OP_J, TRIG_ENGINE_J, TRIG_EXTERNAL,
                                       triggerSlopeId(config_.digitizer.trigger_slope),
                                       kAlazarExternalTriggerLevelCode,
                                       TRIG_ENGINE_K, TRIG_DISABLE, TRIGGER_SLOPE_POSITIVE, 128),
             "AlazarSetTriggerOperation", error) ||
      !check(AlazarSetExternalTrigger(
                 impl_->board, DC_COUPLING,
                 externalTriggerRangeId(impl_->capabilities->external_trigger_range)),
             "AlazarSetExternalTrigger", error) ||
      !check(AlazarSetTriggerDelay(impl_->board, config_.digitizer.trigger_delay_samples),
             "AlazarSetTriggerDelay", error) ||
      !check(AlazarSetTriggerTimeOut(impl_->board, 0), "AlazarSetTriggerTimeOut", error) ||
      !check(AlazarConfigureAuxIO(
                 impl_->board, AUX_IN_TRIGGER_ENABLE, TRIGGER_SLOPE_POSITIVE),
             "AlazarConfigureAuxIO", error)) {
    return false;
  }
  U32 max_samples = 0;
  if (!check(AlazarGetChannelInfo(impl_->board, &max_samples, &impl_->bits_per_sample),
             "AlazarGetChannelInfo", error)) {
    return false;
  }
  if (impl_->bits_per_sample != impl_->capabilities->bits_per_sample) {
    error = impl_->capabilities->display_name +
            " did not report the expected 12-bit left-aligned sample format";
    return false;
  }
  error.clear();
  return true;
#else
  error = "Alazar SDK support is not built";
  return false;
#endif
}

void AlazarDigitizer::releaseBuffers() {
#if FMCW_HAS_ALAZAR_SDK
  impl_->compatibility_batch.reset();
  impl_->compatibility_record_index = 0;
  if (impl_->lease_state) {
    std::lock_guard<std::mutex> lease_lock(impl_->lease_state->mutex);
    impl_->lease_state->repost_enabled = false;
    impl_->lease_state->released_indices.clear();
    impl_->lease_state->condition.notify_all();
  }
  impl_->posted_indices.clear();
  impl_->buffers.clear();
  impl_->lease_state.reset();
#endif
}

}  // namespace fmcw
