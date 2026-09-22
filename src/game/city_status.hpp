#pragma once

#include "game/account_codec.hpp"
#include "video/character_frame.hpp"

#include <cstdint>

namespace ghostbusters::game {

// The two status bytes are independent state and remain separate from the
// account balance at this native boundary.
struct CityStatusInput {
    std::uint8_t status5a = 0;
    std::uint8_t status5b = 0;
    AccountBalanceBytes balance{};
};

// Returns a copy of the input frame with the status row cells updated only
// while state is in the active range [0x12, 0x2A) and the notice is idle.
[[nodiscard]] video::CharacterFrame update_city_status(
    std::uint8_t state, bool notice_busy, CityStatusInput input,
    const video::CharacterFrame& frame);

} // namespace ghostbusters::game
