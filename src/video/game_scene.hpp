#pragma once

#include "video/character_frame.hpp"
#include "video/sprite_layer.hpp"

#include <cstdint>
#include <optional>

namespace ghostbusters::video {

struct GameSceneOptions {
    // Low three bits of the playfield's $D016 value. The game's normal
    // unscrolled setting is seven.
    std::uint8_t horizontal_scroll = 7;
    std::optional<std::uint8_t> notice_phase;
    bool split_band = false;
    // The gameplay display normally leaves CSEL clear ($D016 bit 3).
    bool horizontal_border = true;
    std::uint8_t border_color = 0;
    // The rooftop sequence keeps the notice strip black instead of blue.
    std::uint8_t notice_background = 6;
};

// Render the gameplay character matrix and VIC-coordinate sprite snapshot into
// the 320x200 viewport whose raster origin is (24, 50).
[[nodiscard]] IndexedImage render_game_scene(const CharacterFrame& characters,
                                             const SpriteLayer& sprites,
                                             GameSceneOptions options = {});

} // namespace ghostbusters::video
