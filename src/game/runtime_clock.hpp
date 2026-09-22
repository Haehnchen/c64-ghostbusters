#pragma once
#include <cstdint>

namespace ghostbusters::game {
// $8ED4-$8EDC, once per non-title raster IRQ. Zero is deliberately not repaired.
[[nodiscard]] constexpr std::uint8_t advance_game_random(std::uint8_t value) noexcept
{
    const auto feedback = static_cast<std::uint8_t>(((value << 3U) ^ value) >> 7U) & 1U;
    return static_cast<std::uint8_t>((value << 1U) | feedback);
}
// $70EE-$70F2, after the IRQ: the common frame counter stops when $47 bit7 is set.
[[nodiscard]] constexpr std::uint8_t advance_game_frame(std::uint8_t frame,
                                                       std::uint8_t disabled47) noexcept
{
    return (disabled47 & 0x80U) != 0 ? frame : static_cast<std::uint8_t>(frame + 1U);
}
}
