#include "game/text_irq.hpp"
#include "assets/capture_data.hpp"
#include "game/title_scroller.hpp"
#include "game/runtime_clock.hpp"
#include "game/pal_counter.hpp"
#include <stdexcept>

namespace ghostbusters::game {
void update_text_irq(const assets::Payload& payload, GameResetState& state)
{
    auto& z = state.zero;
    auto& v = state.vic;
    if (z[0x3A] >= 0x12) throw std::invalid_argument("Text IRQ requires state below 18");
    z[6] = advance_game_random(z[6]);
    z[8] = state.runtime[0x73] ? advance_pal_counter(z[8]) : static_cast<std::uint8_t>(z[8]+1);
    TitleScroller scroller;
    scroller.phase = (unsigned(z[5] & 3) << 8) | z[4];
    scroller.tick(z[2]);
    scroller.write_row(payload,state.characters);
    z[4] = static_cast<std::uint8_t>(scroller.phase);
    z[5] = static_cast<std::uint8_t>(scroller.phase >> 8);
    // $97A1's scratch bytes survive its return. $DC is overwritten again
    // by the eight sprite-high rotations below; $DD/$DE remain observable.
    const auto delta = static_cast<std::uint16_t>((scroller.phase-0x3B8) & 0x3FF);
    z[0xDD] = static_cast<std::uint8_t>(delta >> 8);
    z[0xDE] = static_cast<std::uint8_t>((z[0xDD] & 1) << 7);
    v[0x11] &= 0x3F; // $97F0/$9800 clears extended background mode.
    v[0x16] = static_cast<std::uint8_t>((v[0x16] & 0xE0) | (z[4] & 7) | 0x17);
    v[0x15] = 0xFF;
    z[0xDC] = v[0x1C];
    const assets::CaptureData capture_data(payload);
    for (int sprite = 7; sprite >= 0; --sprite) {
        const auto x = z[0xA0+sprite*2];
        z[0xDC] = static_cast<std::uint8_t>((z[0xDC] << 1) | (x >> 7));
        v[sprite*2] = static_cast<std::uint8_t>((x << 1) | 1);
        v[sprite*2+1] = z[0xA1+sprite*2];
        state.characters.screen[0x3F8+sprite] = z[0x2B+sprite];
        v[0x27+sprite] = static_cast<std::uint8_t>(
            0xF0 | (capture_data.sprite_color(z[0x2B+sprite]) & 15));
    }
    v[0x10] = z[0xDC];
    v[0x21] = static_cast<std::uint8_t>(0xF0 | (z[0x3B] & 15));
    v[0x25] = static_cast<std::uint8_t>(0xF0 | (z[0x1D] & 15));
    v[0x26] = static_cast<std::uint8_t>(0xF0 | (z[0x1E] & 15));
    state.characters.background = z[0x3B] & 15;
    z[0xE2] = 1;
}
}
