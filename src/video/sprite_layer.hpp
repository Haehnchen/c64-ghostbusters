#pragma once

#include "video/character_frame.hpp"

#include <array>
#include <span>

namespace ghostbusters::video {

struct Sprite {
    std::array<std::uint8_t, 63> bitmap{};
    // Coordinates relative to the visible 320x200 image, not VIC registers.
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint8_t color = 1;
    bool enabled = false;
    bool multicolor = false;
    bool expand_x = false;
    bool expand_y = false;
    bool behind_foreground = false;
};

struct SpriteLayer {
    std::array<Sprite, 8> sprites{};
    std::uint8_t multicolor1 = 0; // $D025
    std::uint8_t multicolor2 = 0; // $D026
};

// Composite a static register snapshot. Optional foreground mask is one byte
// per image pixel. Raster changes, collisions and border opening are separate.
void composite_sprites(IndexedImage& image, const SpriteLayer& layer,
                       std::span<const std::uint8_t> foreground = {});

} // namespace ghostbusters::video
