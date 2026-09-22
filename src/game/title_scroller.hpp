#pragma once

#include "assets/payload.hpp"
#include "video/character_frame.hpp"

#include <cstdint>

namespace ghostbusters::game {

struct TitleScroller {
    // The low ten bits model the original $05:$04 phase. Keeping phase public
    // permits direct initialization from a captured C64 state/oracle.
    std::uint16_t phase = 0;

    // One call represents one physical IRQ update, not elapsed wall-clock time.
    void tick(std::uint8_t control = 0);

    [[nodiscard]] std::uint8_t fine_scroll() const;
    [[nodiscard]] std::uint8_t source_index() const;

    // Update only the final 40 screen cells. The title's row colors remain
    // owned by the initial title setup and are intentionally left untouched.
    void write_row(const assets::Payload& payload,
                   video::CharacterFrame& frame) const;
};

} // namespace ghostbusters::game
