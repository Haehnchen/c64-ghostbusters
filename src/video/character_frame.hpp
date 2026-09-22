#pragma once

#include <array>
#include <cstdint>

namespace ghostbusters::video {

struct CharacterFrame {
    std::array<std::uint8_t, 1024> screen{};
    std::array<std::uint8_t, 2048> charset{};
    std::array<std::uint8_t, 1024> colors{};
    std::uint8_t background = 0;
    std::uint8_t multicolor1 = 1;
    std::uint8_t multicolor2 = 2;
    bool multicolor = true;
};

using IndexedImage = std::array<std::uint8_t, 320 * 200>;

// Render the unscrolled 40x25 character layer into its 320x200 logical image.
// Borders, raster effects, and sprites are intentionally outside this layer.
// Optional VIC foreground mask uses glyph bits, independently of palette colors.
[[nodiscard]] IndexedImage render_characters(const CharacterFrame& frame,
                                              IndexedImage* foreground_mask = nullptr);

} // namespace ghostbusters::video
