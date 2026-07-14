#pragma once

#include <cstdint>

namespace fmcw {

std::int16_t alazarLeftAlignedSampleToSignedInt16(std::uint16_t sample_value,
                                                  std::uint8_t bits_per_sample) noexcept;

}  // namespace fmcw
