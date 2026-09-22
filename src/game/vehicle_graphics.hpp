#pragma once
#include "assets/payload.hpp"
#include "video/character_frame.hpp"
#include <cstdint>

namespace ghostbusters::game {
void prepare_vehicle_charset(const assets::Payload& payload,
                             video::CharacterFrame& frame, std::uint8_t vehicle);
void draw_vehicle_grid(const assets::Payload& payload, video::CharacterFrame& frame);
// $7667 and $35E6: build mirrored vehicle glyphs and the 12x16 character grid.
void prepare_vehicle_graphics(const assets::Payload& payload,
                              video::CharacterFrame& frame, std::uint8_t vehicle);
}
