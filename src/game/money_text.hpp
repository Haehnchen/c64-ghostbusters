#pragma once

#include "game/account_codec.hpp"
#include <array>

#include <cstdint>
#include <vector>

namespace ghostbusters::game {
// $9D24/$9D74 result before the optional $9063 leading-space compaction.
[[nodiscard]] std::array<std::uint8_t, 8> format_money_field(AccountBalanceBytes balance);

// Format the balance bytes used by the game ($57, $58, $59) as the bounded
// text-script buffer produced by $9D24 followed by $9063. The returned vector
// always includes its terminating 0xFF.
[[nodiscard]] std::vector<std::uint8_t>
format_money(AccountBalanceBytes balance);

} // namespace ghostbusters::game
