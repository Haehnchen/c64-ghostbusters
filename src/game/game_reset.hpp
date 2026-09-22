#pragma once

#include "assets/payload.hpp"
#include "game/input_frame.hpp"
#include "video/character_frame.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// The shared memory/register view of title, reset and their nested IRQs. The
// runtime array is addressed as $EA00 + index, while VIC and SID are addressed
// as $D000 + index and $D400 + index respectively.
struct GameResetState {
    std::array<std::uint8_t, 256> zero{};
    // Includes title command scratch $EA89, the saved audio text row
    // $EA8A-$EAB1 and accepted start key $EAB2.
    std::array<std::uint8_t, 0xD0> runtime{};
    std::array<std::uint8_t, 47> vic{};
    std::array<std::uint8_t, 32> sid{};
    video::CharacterFrame characters{};
};

struct ResetSidWrite {
    std::uint8_t reg = 0;
    std::uint8_t value = 0;

    [[nodiscard]] bool operator==(const ResetSidWrite&) const = default;
};

struct ResetResult {
    // The hardware reset writes SID voice/control registers in descending
    // register order.  Keeping the trace separate from the final register
    // image preserves that observable order for audio adapters.
    std::vector<ResetSidWrite> sid_writes{};
};

// Apply the bounded reset effects. Full reset copies all 18 initial-state
// bytes; partial reset copies the first 12 and preserves the remaining tail.
// GameReset::none is a strict no-op, including the returned SID write trace.
[[nodiscard]] ResetResult apply_game_reset(const assets::Payload& payload,
                                            GameReset kind,
                                            GameResetState& state);

} // namespace ghostbusters::game
