#include "core/digitizer_capabilities.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fmcw {
namespace {

bool nearlyEqual(double left, double right) {
  return std::abs(left - right) <= std::max(1.0, std::abs(right) * 1.0e-9);
}

DigitizerBoardCapabilities makeCapabilities(
    std::string profile_id, std::string display_name, std::uint32_t sdk_board_kind,
    std::vector<double> sample_rates_hz, std::vector<double> input_ranges_volts,
    std::uint32_t minimum_record_samples, std::uint32_t record_resolution_samples,
    std::uint32_t pretrigger_alignment_samples, std::uint32_t maximum_npt_pretrigger_samples,
    std::uint32_t trigger_delay_alignment_samples,
    AlazarExternalTriggerRange external_trigger_range, bool fifo_only_streaming_supported) {
  DigitizerBoardCapabilities capabilities;
  capabilities.profile_id = std::move(profile_id);
  capabilities.display_name = std::move(display_name);
  capabilities.sdk_board_kind = sdk_board_kind;
  capabilities.bits_per_sample = 12U;
  capabilities.aux_trigger_enable_supported = true;
  capabilities.fifo_only_streaming_supported = fifo_only_streaming_supported;
  capabilities.external_trigger_range = external_trigger_range;
  capabilities.sample_rates_hz = std::move(sample_rates_hz);
  capabilities.input_ranges_volts = std::move(input_ranges_volts);
  capabilities.impedances_ohms = {50U};
  capabilities.minimum_record_samples = minimum_record_samples;
  capabilities.record_resolution_samples = record_resolution_samples;
  capabilities.pretrigger_alignment_samples = pretrigger_alignment_samples;
  capabilities.maximum_npt_pretrigger_samples = maximum_npt_pretrigger_samples;
  capabilities.single_channel_trigger_delay_alignment_samples =
      trigger_delay_alignment_samples;
  return capabilities;
}

}  // namespace

const std::vector<DigitizerBoardCapabilities>& digitizerBoardCapabilities() {
  static const std::vector<DigitizerBoardCapabilities> capabilities = {
      makeCapabilities(
          "ats9371", "ATS9371", 33U,
          {1.0e3, 2.0e3, 5.0e3, 10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3,
           500.0e3, 1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6,
           200.0e6, 500.0e6, 800.0e6, 1.0e9},
          {0.4}, 256U, 128U, 128U, 8176U, 16U,
          AlazarExternalTriggerRange::Ttl, true),
      makeCapabilities(
          "ats9120", "ATS9120", 32U,
          {10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3, 500.0e3, 1.0e6, 2.0e6,
           5.0e6, 10.0e6, 20.0e6},
          {0.04, 0.05, 0.08, 0.1, 0.2, 0.4, 0.5, 0.8, 1.0, 2.0, 4.0},
          256U, 32U, 32U, 4080U, 8U, AlazarExternalTriggerRange::Ttl, true),
      makeCapabilities(
          "ats9130", "ATS9130", 34U,
          {10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3, 500.0e3, 1.0e6, 2.0e6,
           5.0e6, 10.0e6, 25.0e6, 50.0e6},
          {0.04, 0.05, 0.08, 0.1, 0.2, 0.4, 0.5, 0.8, 1.0, 2.0, 4.0},
          256U, 32U, 32U, 4080U, 8U, AlazarExternalTriggerRange::Ttl, true),
      makeCapabilities(
          "ats9350", "ATS9350", 14U,
          {1.0e3, 2.0e3, 5.0e3, 10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3,
           500.0e3, 1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6,
           125.0e6, 250.0e6, 500.0e6},
          {0.04, 0.1, 0.2, 0.4, 1.0, 2.0, 4.0},
          256U, 32U, 32U, 4080U, 8U, AlazarExternalTriggerRange::FiveVolts,
          false),
      makeCapabilities(
          "ats9351", "ATS9351", 18U,
          {1.0e3, 2.0e3, 5.0e3, 10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3,
           500.0e3, 1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6,
           125.0e6, 250.0e6, 500.0e6},
          {0.4}, 256U, 32U, 32U, 4080U, 8U,
          AlazarExternalTriggerRange::FiveVolts, false),
      makeCapabilities(
          "ats9352", "ATS9352", 35U,
          {1.0e3, 2.0e3, 5.0e3, 10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3,
           500.0e3, 1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6,
           125.0e6, 250.0e6, 500.0e6},
          {0.04, 0.1, 0.2, 0.4, 1.0, 2.0, 4.0},
          256U, 32U, 32U, 4080U, 8U, AlazarExternalTriggerRange::FiveVolts,
          false),
      makeCapabilities(
          "ats9353", "ATS9353", 44U,
          {1.0e3, 2.0e3, 5.0e3, 10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3,
           500.0e3, 1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6,
           125.0e6, 250.0e6, 500.0e6},
          {0.4}, 256U, 32U, 32U, 4080U, 8U,
          AlazarExternalTriggerRange::FiveVolts, false),
      makeCapabilities(
          "ats9360", "ATS9360", 25U,
          {1.0e3, 2.0e3, 5.0e3, 10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3,
           500.0e3, 1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6,
           200.0e6, 500.0e6, 800.0e6, 1.0e9, 1.2e9, 1.5e9, 1.8e9},
          {0.4}, 256U, 128U, 128U, 8176U, 16U,
          AlazarExternalTriggerRange::Ttl, true),
      makeCapabilities(
          "ats9362", "ATS9362", 58U,
          {1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6, 200.0e6,
           500.0e6, 750.0e6},
          {0.4}, 256U, 128U, 128U, 8176U, 16U,
          AlazarExternalTriggerRange::Ttl, false),
      makeCapabilities(
          "ats9364", "ATS9364", 53U,
          {1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6, 200.0e6,
           500.0e6, 800.0e6, 1.0e9},
          {0.4}, 256U, 128U, 128U, 8176U, 16U,
          AlazarExternalTriggerRange::Ttl, false),
      makeCapabilities(
          "ats9373", "ATS9373", 29U,
          {1.0e3, 2.0e3, 5.0e3, 10.0e3, 20.0e3, 50.0e3, 100.0e3, 200.0e3,
           500.0e3, 1.0e6, 2.0e6, 5.0e6, 10.0e6, 20.0e6, 50.0e6, 100.0e6,
           200.0e6, 500.0e6, 800.0e6, 1.0e9, 1.2e9, 1.5e9, 2.0e9, 2.4e9,
           3.0e9, 3.6e9, 4.0e9},
          {0.4}, 256U, 128U, 128U, 8176U, 16U,
          AlazarExternalTriggerRange::Ttl, true),
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

const DigitizerBoardCapabilities* findDigitizerBoardCapabilitiesBySdkBoardKind(
    std::uint32_t sdk_board_kind) {
  const auto& capabilities = digitizerBoardCapabilities();
  const auto found = std::find_if(
      capabilities.begin(), capabilities.end(),
      [sdk_board_kind](const DigitizerBoardCapabilities& item) {
        return item.sdk_board_kind == sdk_board_kind;
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

std::uint32_t nearestSupportedRecordLength(const DigitizerBoardCapabilities& capabilities,
                                           std::uint32_t requested_samples) {
  const std::uint64_t resolution = capabilities.record_resolution_samples;
  if (resolution == 0U) {
    return capabilities.minimum_record_samples;
  }

  const std::uint64_t minimum = capabilities.minimum_record_samples;
  const std::uint64_t aligned_minimum = ((minimum + resolution - 1U) / resolution) * resolution;
  if (requested_samples <= aligned_minimum) {
    return static_cast<std::uint32_t>(aligned_minimum);
  }

  const std::uint64_t requested = requested_samples;
  const std::uint64_t lower = requested - (requested % resolution);
  const std::uint64_t maximum = std::numeric_limits<std::uint32_t>::max();
  const std::uint64_t upper = lower <= maximum - resolution ? lower + resolution : lower;
  const bool use_lower = upper == lower || requested - lower < upper - requested;
  const std::uint64_t nearest = use_lower ? lower : upper;
  return static_cast<std::uint32_t>(std::max(nearest, aligned_minimum));
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
