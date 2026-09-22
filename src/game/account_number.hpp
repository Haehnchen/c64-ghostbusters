#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace ghostbusters::game {

// Pack at most eight input bytes exactly as the original $9CFC routine does.
// Input is consumed up to the first NUL; each consumed byte contributes only
// its low nibble. The result is a big-endian four-byte value.
[[nodiscard]] std::array<std::uint8_t, 4>
pack_account_number(std::span<const std::uint8_t> input);

} // namespace ghostbusters::game
