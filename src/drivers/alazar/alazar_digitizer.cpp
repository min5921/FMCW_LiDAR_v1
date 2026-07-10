#if defined(FMCW_TARGET_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "drivers/alazar/alazar_digitizer.h"

#include "core/config_validation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
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

#endif

}  // namespace

struct AlazarDigitizer::Impl {
#if FMCW_HAS_ALAZAR_SDK
  HANDLE board = nullptr;
  U8 bits_per_sample = 0;
  std::vector<U16*> buffers;
  U32 bytes_per_buffer = 0;
  U32 records_per_buffer = 0;
  U32 active_buffer = 0;
  U32 active_record = 0;
  bool buffer_ready = false;
  bool async_prepared = false;
  std::uint64_t next_frame_id = 1;
#endif
};

AlazarDigitizer::AlazarDigitizer() : impl_(std::make_unique<Impl>()) {
  telemetry_.device.detail = sdkAvailable() ? "Alazar SDK available" : "Alazar SDK not linked";
}

AlazarDigitizer::~AlazarDigitizer() { disconnect(); }

bool AlazarDigitizer::sdkAvailable() { return FMCW_HAS_ALAZAR_SDK != 0; }

std::string AlazarDigitizer::name() const { return "AlazarTech ATS AutoDMA digitizer"; }

DigitizerTelemetry AlazarDigitizer::telemetry() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return telemetry_;
}

bool AlazarDigitizer::configure(const SystemConfig& config, std::string& error) {
  const auto validation = ConfigValidator::validate(config);
  if (validation.hasErrors()) {
    error = "Alazar digitizer rejected invalid system configuration";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (telemetry_.device.running) {
    error = "Cannot configure Alazar digitizer while acquisition is running";
    return false;
  }
  if (std::abs(config.digitizer.sample_rate_hz - 1.0e9) > 1.0 ||
      std::abs(config.digitizer.input_range_volts - 0.4) > 1.0e-9 ||
      config.digitizer.impedance_ohms != 50 || config.digitizer.trigger_source != TriggerSource::External) {
    error = "Phase 3 Alazar hardware adapter currently supports 1 GS/s, +/-400 mV, 50 ohm, external trigger";
    return false;
  }
  config_ = config;
  configured_ = true;
  telemetry_.device.detail = "Alazar configuration cached for channel " + toString(config_.digitizer.channel);
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
  impl_->board = AlazarGetBoardBySystemID(config_.digitizer.system_id, config_.digitizer.board_id);
  if (impl_->board == nullptr) {
    error = "AlazarGetBoardBySystemID returned no board for the configured system/board id";
    return false;
  }
  telemetry_.device.connected = true;
  if (!configureBoard(error)) {
    impl_->board = nullptr;
    telemetry_.device.connected = false;
    return false;
  }
  telemetry_.device.ready = true;
  telemetry_.device.detail = "Alazar board connected and configured";
  error.clear();
  return true;
#else
  error = "Alazar SDK support is not built. Set ALAZAR_SDK_ROOT and reconfigure CMake";
  return false;
#endif
}

void AlazarDigitizer::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
  if (impl_->async_prepared && impl_->board != nullptr) {
    AlazarAbortAsyncRead(impl_->board);
    impl_->async_prepared = false;
  }
  releaseBuffers();
  impl_->board = nullptr;
#endif
  telemetry_.device = {};
  telemetry_.device.detail = sdkAvailable() ? "Alazar disconnected" : "Alazar SDK not linked";
}

bool AlazarDigitizer::start(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
  if (!configured_ || impl_->board == nullptr || !telemetry_.device.ready || telemetry_.device.running) {
    error = "Alazar digitizer is not ready or is already running";
    return false;
  }
  releaseBuffers();
  impl_->records_per_buffer = config_.digitizer.acquisition_mode == AcquisitionMode::Finite ?
      1U : config_.digitizer.records_per_buffer;
  const std::uint64_t bytes = static_cast<std::uint64_t>(config_.digitizer.sample_point) *
                              impl_->records_per_buffer * sizeof(U16);
  if (bytes == 0 || bytes > std::numeric_limits<U32>::max()) {
    error = "Alazar DMA buffer size exceeds the ATS API limit";
    return false;
  }
  impl_->bytes_per_buffer = static_cast<U32>(bytes);
  impl_->buffers.resize(config_.digitizer.dma_buffer_count, nullptr);
  for (auto& buffer : impl_->buffers) {
    buffer = AlazarAllocBufferU16(impl_->board, impl_->bytes_per_buffer);
    if (buffer == nullptr) {
      error = "AlazarAllocBufferU16 failed";
      releaseBuffers();
      return false;
    }
  }
  if (!check(AlazarSetRecordSize(impl_->board, config_.digitizer.pre_trigger_samples,
                                 config_.digitizer.post_trigger_samples), "AlazarSetRecordSize", error)) {
    releaseBuffers();
    return false;
  }
  const U32 records_per_acquisition = config_.digitizer.acquisition_mode == AcquisitionMode::Finite ?
      config_.digitizer.finite_frame_count : 0x7FFFFFFFU;
  U32 flags = ADMA_EXTERNAL_STARTCAPTURE | ADMA_NPT;
  if (config_.digitizer.fifo_only_streaming) {
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
  for (auto* buffer : impl_->buffers) {
    if (!check(AlazarPostAsyncBuffer(impl_->board, buffer, impl_->bytes_per_buffer),
               "AlazarPostAsyncBuffer", error)) {
      AlazarAbortAsyncRead(impl_->board);
      impl_->async_prepared = false;
      releaseBuffers();
      return false;
    }
  }
  if (!check(AlazarStartCapture(impl_->board), "AlazarStartCapture", error)) {
    AlazarAbortAsyncRead(impl_->board);
    impl_->async_prepared = false;
    releaseBuffers();
    return false;
  }
  impl_->active_buffer = 0;
  impl_->active_record = 0;
  impl_->buffer_ready = false;
  impl_->next_frame_id = 1;
  telemetry_.frames_received = 0;
  telemetry_.dma_buffer_drops = 0;
  telemetry_.trigger_misses = 0;
  telemetry_.device.running = true;
  telemetry_.device.detail = "Alazar NPT AutoDMA acquisition active";
  error.clear();
  return true;
#else
  error = "Alazar SDK support is not built";
  return false;
#endif
}

FrameWaitResult AlazarDigitizer::waitForFrame(RawFrame& frame, std::chrono::milliseconds timeout,
                                              std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
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
  auto* buffer = impl_->buffers[impl_->active_buffer];
  if (!impl_->buffer_ready) {
    const auto wait_ms = static_cast<U32>(std::clamp<std::int64_t>(timeout.count(), 0, std::numeric_limits<U32>::max()));
    const auto result = AlazarWaitAsyncBufferComplete(impl_->board, buffer, wait_ms);
    if (result == ApiWaitTimeout) {
      error.clear();
      return FrameWaitResult::Timeout;
    }
    if (result == ApiBufferOverflow) {
      ++telemetry_.dma_buffer_drops;
    }
    if (!check(result, "AlazarWaitAsyncBufferComplete", error)) {
      telemetry_.device.detail = error;
      return FrameWaitResult::Error;
    }
    impl_->buffer_ready = true;
    impl_->active_record = 0;
  }

  frame = {};
  frame.samples.resize(config_.digitizer.sample_point);
  const auto offset = static_cast<std::size_t>(impl_->active_record) * config_.digitizer.sample_point;
  const std::int32_t midpoint = 1 << (impl_->bits_per_sample - 1U);
  for (std::size_t index = 0; index < frame.samples.size(); ++index) {
    const auto centered = static_cast<std::int32_t>(buffer[offset + index]) - midpoint;
    frame.samples[index] = static_cast<std::int16_t>(std::clamp(centered, -32768, 32767));
  }

  const auto frame_id = impl_->next_frame_id++;
  frame.metadata.frame_kind = FrameKind::FullChirpPeriod;
  frame.metadata.frame_id = frame_id;
  frame.metadata.host_timestamp_ns = nowNs();
  frame.metadata.trigger.sequence = frame_id;
  frame.metadata.trigger.timestamp_ns = frame.metadata.host_timestamp_ns;
  frame.metadata.trigger.valid = true;
  frame.metadata.channel = config_.digitizer.channel;
  frame.metadata.sample_rate_hz = config_.digitizer.sample_rate_hz;
  frame.metadata.record_length = config_.digitizer.sample_point;
  frame.metadata.pre_trigger_samples = config_.digitizer.pre_trigger_samples;
  frame.metadata.post_trigger_samples = config_.digitizer.post_trigger_samples;
  frame.metadata.up_segment = config_.chirp_segmentation.up_segment;
  frame.metadata.down_segment = config_.chirp_segmentation.down_segment;

  ++telemetry_.frames_received;
  ++impl_->active_record;
  if (impl_->active_record >= impl_->records_per_buffer) {
    const bool finite_complete = config_.digitizer.acquisition_mode == AcquisitionMode::Finite &&
                                 telemetry_.frames_received >= config_.digitizer.finite_frame_count;
    if (!finite_complete) {
      if (!check(AlazarPostAsyncBuffer(impl_->board, buffer, impl_->bytes_per_buffer),
                 "AlazarPostAsyncBuffer(repost)", error)) {
        return FrameWaitResult::Error;
      }
      impl_->active_buffer = (impl_->active_buffer + 1U) % static_cast<U32>(impl_->buffers.size());
    }
    impl_->buffer_ready = false;
  }
  error.clear();
  return FrameWaitResult::FrameReady;
#else
  static_cast<void>(frame);
  static_cast<void>(timeout);
  error = "Alazar SDK support is not built";
  return FrameWaitResult::Error;
#endif
}

bool AlazarDigitizer::abort(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
  if (impl_->async_prepared && impl_->board != nullptr) {
    if (!check(AlazarAbortAsyncRead(impl_->board), "AlazarAbortAsyncRead", error)) {
      return false;
    }
    impl_->async_prepared = false;
  }
#endif
  telemetry_.device.running = false;
  telemetry_.device.detail = "Alazar acquisition aborted";
  error.clear();
  return true;
}

bool AlazarDigitizer::stop(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
#if FMCW_HAS_ALAZAR_SDK
  if (impl_->async_prepared && impl_->board != nullptr) {
    if (!check(AlazarAbortAsyncRead(impl_->board), "AlazarAbortAsyncRead", error)) {
      return false;
    }
    impl_->async_prepared = false;
  }
  releaseBuffers();
#endif
  telemetry_.device.running = false;
  telemetry_.device.ready = telemetry_.device.connected;
  telemetry_.device.detail = telemetry_.device.connected ? "Alazar ready" : "Alazar disconnected";
  error.clear();
  return true;
}

bool AlazarDigitizer::configureBoard(std::string& error) {
#if FMCW_HAS_ALAZAR_SDK
  if (!check(AlazarSetCaptureClock(impl_->board, INTERNAL_CLOCK, SAMPLE_RATE_1000MSPS,
                                   CLOCK_EDGE_RISING, 0), "AlazarSetCaptureClock", error) ||
      !check(AlazarInputControlEx(impl_->board, channelMask(config_.digitizer.channel),
                                  couplingId(config_.digitizer.coupling), INPUT_RANGE_PM_400_MV,
                                  IMPEDANCE_50_OHM), "AlazarInputControlEx", error) ||
      !check(AlazarSetTriggerOperation(impl_->board, TRIG_ENGINE_OP_J, TRIG_ENGINE_J, TRIG_EXTERNAL,
                                       triggerSlopeId(config_.digitizer.trigger_slope), 128,
                                       TRIG_ENGINE_K, TRIG_DISABLE, TRIGGER_SLOPE_POSITIVE, 128),
             "AlazarSetTriggerOperation", error) ||
      !check(AlazarSetExternalTrigger(impl_->board, DC_COUPLING, ETR_TTL),
             "AlazarSetExternalTrigger", error) ||
      !check(AlazarSetTriggerDelay(impl_->board, config_.digitizer.trigger_delay_samples),
             "AlazarSetTriggerDelay", error) ||
      !check(AlazarSetTriggerTimeOut(impl_->board, 0), "AlazarSetTriggerTimeOut", error) ||
      !check(AlazarConfigureAuxIO(impl_->board, AUX_IN_TRIGGER_ENABLE, 1), "AlazarConfigureAuxIO", error)) {
    return false;
  }
  U32 max_samples = 0;
  if (!check(AlazarGetChannelInfo(impl_->board, &max_samples, &impl_->bits_per_sample),
             "AlazarGetChannelInfo", error)) {
    return false;
  }
  if (impl_->bits_per_sample <= 8U || impl_->bits_per_sample > 16U) {
    error = "Phase 3 Alazar adapter requires a 9..16 bit board using U16 DMA buffers";
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
  for (auto*& buffer : impl_->buffers) {
    if (buffer != nullptr && impl_->board != nullptr) {
      AlazarFreeBufferU16(impl_->board, buffer);
      buffer = nullptr;
    }
  }
  impl_->buffers.clear();
  impl_->buffer_ready = false;
#endif
}

}  // namespace fmcw
