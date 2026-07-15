#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fmcw {

inline constexpr std::uint32_t kAlazarSystemId = 1;
inline constexpr std::uint32_t kAlazarBoardId = 1;
inline constexpr std::uint32_t kAlazarMinimumPostTriggerSamples = 64;

struct DigitizerBoardCapabilities {
  std::string profile_id;
  std::string display_name;
  std::vector<double> sample_rates_hz;
  std::vector<double> input_ranges_volts;
  std::vector<std::uint32_t> impedances_ohms;
  std::uint32_t minimum_record_samples = 0;
  std::uint32_t record_resolution_samples = 1;
  std::uint32_t pretrigger_alignment_samples = 1;
  std::uint32_t maximum_npt_pretrigger_samples = 0;
  std::uint32_t single_channel_trigger_delay_alignment_samples = 1;
};

const std::vector<DigitizerBoardCapabilities>& digitizerBoardCapabilities();
const DigitizerBoardCapabilities* findDigitizerBoardCapabilities(std::string_view profile_id);
bool supportsSampleRate(const DigitizerBoardCapabilities& capabilities, double sample_rate_hz);
bool supportsRecordLength(const DigitizerBoardCapabilities& capabilities, std::uint32_t record_samples);
bool supportsInputRange(const DigitizerBoardCapabilities& capabilities, double input_range_volts);
bool supportsImpedance(const DigitizerBoardCapabilities& capabilities, std::uint32_t impedance_ohms);
std::uint32_t alazarTriggerLevelCode(double percent_full_scale);
double alazarTriggerLevelPercent(std::uint32_t code);

}  // namespace fmcw
