#include "core/config_types.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <utility>

namespace fmcw {
namespace {

std::string lower(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return result;
}

template <typename Enum>
bool parseEnum(std::string_view text, Enum& value, std::initializer_list<std::pair<const char*, Enum>> options) {
  const auto normalized = lower(text);
  for (const auto& option : options) {
    if (normalized == option.first) {
      value = option.second;
      return true;
    }
  }
  return false;
}

}  // namespace

std::string toString(DigitizerChannel value) { return value == DigitizerChannel::A ? "a" : "b"; }
std::string toString(Coupling value) { return value == Coupling::Ac ? "ac" : "dc"; }
std::string toString(TriggerSource value) { return value == TriggerSource::External ? "external" : "internal"; }
std::string toString(TriggerSlope value) { return value == TriggerSlope::Rising ? "rising" : "falling"; }

std::string toString(AcquisitionMode value) {
  switch (value) {
    case AcquisitionMode::Single: return "single";
    case AcquisitionMode::Continuous: return "continuous";
    case AcquisitionMode::Finite: return "finite";
  }
  return "continuous";
}

std::string toString(AcquisitionSource value) {
  switch (value) {
    case AcquisitionSource::Simulator: return "simulator";
    case AcquisitionSource::Alazar: return "alazar";
    case AcquisitionSource::Replay: return "replay";
  }
  return "simulator";
}

std::string toString(ChirpTriggerMode) { return "up_chirp_only"; }

std::string toString(WindowFunction value) {
  switch (value) {
    case WindowFunction::Hann: return "hann";
    case WindowFunction::Hamming: return "hamming";
    case WindowFunction::Blackman: return "blackman";
    case WindowFunction::Rectangular: return "rectangular";
  }
  return "hann";
}

std::string toString(SegmentPolarity value) {
  return value == SegmentPolarity::Normal ? "normal" : "invert_down";
}

std::string toString(FftBackendKind value) { return value == FftBackendKind::Cuda ? "cuda" : "fftw"; }

std::string toString(EdfaMode value) {
  switch (value) {
    case EdfaMode::None: return "none";
    case EdfaMode::Manual: return "manual";
    case EdfaMode::Controlled: return "controlled";
  }
  return "none";
}

std::string toString(EdfaControlMode value) {
  switch (value) {
    case EdfaControlMode::Acc: return "acc";
    case EdfaControlMode::Apc: return "apc";
    case EdfaControlMode::Agc: return "agc";
  }
  return "apc";
}

std::string toString(OpticalPowerUnit value) { return value == OpticalPowerUnit::Milliwatt ? "mw" : "dbm"; }
std::string toString(QueueOverflowPolicy) { return "stop_acquisition"; }

std::string toString(UdpBackpressurePolicy value) {
  switch (value) {
    case UdpBackpressurePolicy::LatestFrame: return "latest_frame";
    case UdpBackpressurePolicy::PreserveFrames: return "preserve_frames";
    case UdpBackpressurePolicy::StopSending: return "stop_sending";
  }
  return "latest_frame";
}

std::string toString(SerialParity value) {
  switch (value) {
    case SerialParity::None: return "none";
    case SerialParity::Even: return "even";
    case SerialParity::Odd: return "odd";
  }
  return "none";
}

bool fromString(std::string_view text, DigitizerChannel& value) {
  return parseEnum(text, value, {{"a", DigitizerChannel::A}, {"b", DigitizerChannel::B}});
}

bool fromString(std::string_view text, Coupling& value) {
  return parseEnum(text, value, {{"ac", Coupling::Ac}, {"dc", Coupling::Dc}});
}

bool fromString(std::string_view text, TriggerSource& value) {
  return parseEnum(text, value, {{"external", TriggerSource::External}, {"internal", TriggerSource::Internal}});
}

bool fromString(std::string_view text, TriggerSlope& value) {
  return parseEnum(text, value, {{"rising", TriggerSlope::Rising}, {"falling", TriggerSlope::Falling}});
}

bool fromString(std::string_view text, AcquisitionMode& value) {
  return parseEnum(text, value, {{"single", AcquisitionMode::Single},
                                 {"continuous", AcquisitionMode::Continuous},
                                 {"finite", AcquisitionMode::Finite}});
}

bool fromString(std::string_view text, AcquisitionSource& value) {
  return parseEnum(text, value, {{"simulator", AcquisitionSource::Simulator},
                                 {"alazar", AcquisitionSource::Alazar},
                                 {"replay", AcquisitionSource::Replay}});
}

bool fromString(std::string_view text, ChirpTriggerMode& value) {
  return parseEnum(text, value, {{"up_chirp_only", ChirpTriggerMode::UpChirpOnly}});
}

bool fromString(std::string_view text, WindowFunction& value) {
  return parseEnum(text, value, {{"hann", WindowFunction::Hann},
                                 {"hamming", WindowFunction::Hamming},
                                 {"blackman", WindowFunction::Blackman},
                                 {"rectangular", WindowFunction::Rectangular}});
}

bool fromString(std::string_view text, SegmentPolarity& value) {
  return parseEnum(text, value, {{"normal", SegmentPolarity::Normal},
                                 {"invert_down", SegmentPolarity::InvertDown}});
}

bool fromString(std::string_view text, FftBackendKind& value) {
  return parseEnum(text, value, {{"cuda", FftBackendKind::Cuda}, {"fftw", FftBackendKind::Fftw}});
}

bool fromString(std::string_view text, EdfaMode& value) {
  return parseEnum(text, value, {{"none", EdfaMode::None},
                                 {"manual", EdfaMode::Manual},
                                 {"controlled", EdfaMode::Controlled}});
}

bool fromString(std::string_view text, EdfaControlMode& value) {
  return parseEnum(text, value, {{"acc", EdfaControlMode::Acc},
                                 {"apc", EdfaControlMode::Apc},
                                 {"agc", EdfaControlMode::Agc}});
}

bool fromString(std::string_view text, OpticalPowerUnit& value) {
  return parseEnum(text, value, {{"mw", OpticalPowerUnit::Milliwatt}, {"dbm", OpticalPowerUnit::Dbm}});
}

bool fromString(std::string_view text, QueueOverflowPolicy& value) {
  return parseEnum(text, value, {{"stop_acquisition", QueueOverflowPolicy::StopAcquisition},
                                 {"stop", QueueOverflowPolicy::StopAcquisition}});
}

bool fromString(std::string_view text, UdpBackpressurePolicy& value) {
  return parseEnum(text, value, {{"latest_frame", UdpBackpressurePolicy::LatestFrame},
                                 {"preserve_frames", UdpBackpressurePolicy::PreserveFrames},
                                 {"stop_sending", UdpBackpressurePolicy::StopSending}});
}

bool fromString(std::string_view text, SerialParity& value) {
  return parseEnum(text, value, {{"none", SerialParity::None},
                                 {"even", SerialParity::Even},
                                 {"odd", SerialParity::Odd}});
}

std::uint32_t derivedAScanCount(const SystemConfig& config) {
  return config.digitizer.records_per_buffer;
}

std::uint64_t derivedFramePointCount(const SystemConfig& config) {
  return static_cast<std::uint64_t>(derivedAScanCount(config)) * config.scan.y_line_count;
}

double derivedMcuFrameTimeMs(const SystemConfig& config) {
  if (!(config.scan.scanner_sample_rate_hz > 0.0)) {
    return 0.0;
  }
  return static_cast<double>(derivedFramePointCount(config)) * 1000.0 /
      config.scan.scanner_sample_rate_hz;
}

}  // namespace fmcw
