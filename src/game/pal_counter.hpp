#pragma once

#include <cstdint>

namespace ghostbusters::game {

// Advance the PAL copy of C64 $08. The raster path increments once each
// frame and performs a second increment when the new value has low bits 001,
// skipping phase 1 in every eight-frame group.
[[nodiscard]] constexpr std::uint8_t advance_pal_counter(std::uint8_t counter) noexcept
{
    counter = static_cast<std::uint8_t>(counter + 1U);
    if ((counter & 0x07U) == 1U) {
        counter = static_cast<std::uint8_t>(counter + 1U);
    }
    return counter;
}

} // namespace ghostbusters::game
