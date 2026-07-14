#include "drivers/alazar/alazar_sample_conversion.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace fmcw {

std::int16_t alazarLeftAlignedSampleToSignedInt16(std::uint16_t sample_value,
                                                  std::uint8_t bits_per_sample) noexcept {
  if (bits_per_sample == 0U || bits_per_sample > 16U) {
    return 0;
  }

  const auto sample_bit_shift = static_cast<std::uint8_t>(16U - bits_per_sample);
  const auto sample_code = static_cast<std::uint32_t>(sample_value) >> sample_bit_shift;
  const auto code_zero = static_cast<std::int32_t>(1U << (bits_per_sample - 1U));
  const auto centered_code = static_cast<std::int32_t>(sample_code) - code_zero;
  const auto full_scale_value = centered_code * static_cast<std::int32_t>(1U << sample_bit_shift);
  return static_cast<std::int16_t>(std::clamp(
      full_scale_value,
      static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
      static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
}

}  // namespace fmcw
