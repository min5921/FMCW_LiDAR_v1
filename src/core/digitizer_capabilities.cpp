#include "core/digitizer_capabilities.h"

#include <algorithm>
#include <cmath>

namespace fmcw {
namespace {

bool nearlyEqual(double left, double right) {
  return std::abs(left - right) <= std::max(1.0, std::abs(right) * 1.0e-9);
}

}  // namespace

const std::vector<DigitizerBoardCapabilities>& digitizerBoardCapabilities() {
  static const std::vector<DigitizerBoardCapabilities> capabilities = {
      {"ats9371", "ATS9371 (12-bit)",
       {1.0e3, 2.0e3, 5.0e3, 10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3, 500.0e3,
        1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6, 200.0e6, 500.0e6,
        800.0e6, 1.0e9},
       {0.4}, {50U}, 256U, 128U, 128U, 8176U, 16U},
  };
  return capabilities;
}

const DigitizerBoardCapabilities* findDigitizerBoardCapabilities(std::string_view profile_id) {
  const auto& capabilities = digitizerBoardCapabilities();
  const auto found = std::find_if(capabilities.begin(), capabilities.end(),
                                  [profile_id](const DigitizerBoardCapabilities& item) {
                                    return item.profile_id == profile_id;
                                  });
  return found == capabilities.end() ? nullptr : &*found;
}

bool supportsSampleRate(const DigitizerBoardCapabilities& capabilities, double sample_rate_hz) {
  return std::any_of(capabilities.sample_rates_hz.begin(), capabilities.sample_rates_hz.end(),
                     [sample_rate_hz](double supported) { return nearlyEqual(sample_rate_hz, supported); });
}

bool supportsRecordLength(const DigitizerBoardCapabilities& capabilities,
                          std::uint32_t record_samples) {
  return capabilities.record_resolution_samples > 0U &&
         record_samples >= capabilities.minimum_record_samples &&
         (record_samples % capabilities.record_resolution_samples) == 0U;
}

bool supportsInputRange(const DigitizerBoardCapabilities& capabilities, double input_range_volts) {
  return std::any_of(capabilities.input_ranges_volts.begin(), capabilities.input_ranges_volts.end(),
                     [input_range_volts](double supported) { return nearlyEqual(input_range_volts, supported); });
}

bool supportsImpedance(const DigitizerBoardCapabilities& capabilities, std::uint32_t impedance_ohms) {
  return std::find(capabilities.impedances_ohms.begin(), capabilities.impedances_ohms.end(), impedance_ohms) !=
         capabilities.impedances_ohms.end();
}

}  // namespace fmcw
