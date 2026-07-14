#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fmcw {

inline constexpr std::uint32_t kAlazarSystemId = 1;
inline constexpr std::uint32_t kAlazarBoardId = 1;
inline constexpr std::uint32_t kAts9371RecordResolution = 128;
inline constexpr std::uint32_t kAts9371MaxNptPretrigger = 8176;
inline constexpr std::uint32_t kAts9371SingleChannelTriggerDelayAlignment = 16;

struct DigitizerBoardCapabilities {
  std::string profile_id;
  std::string display_name;
  std::vector<double> sample_rates_hz;
  std::vector<double> input_ranges_volts;
  std::vector<std::uint32_t> impedances_ohms;
};

const std::vector<DigitizerBoardCapabilities>& digitizerBoardCapabilities();
const DigitizerBoardCapabilities* findDigitizerBoardCapabilities(std::string_view profile_id);
bool supportsSampleRate(const DigitizerBoardCapabilities& capabilities, double sample_rate_hz);
bool supportsInputRange(const DigitizerBoardCapabilities& capabilities, double input_range_volts);
bool supportsImpedance(const DigitizerBoardCapabilities& capabilities, std::uint32_t impedance_ohms);
std::uint32_t alazarTriggerLevelCode(double percent_full_scale);
double alazarTriggerLevelPercent(std::uint32_t code);

}  // namespace fmcw
