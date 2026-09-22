#include "video/title_view.hpp"
#include <stdexcept>
#include <algorithm>

namespace ghostbusters::video {

void composite_footer(IndexedImage& image, const CharacterFrame& frame,
                      std::uint16_t phase, std::span<const std::uint8_t> rainbow,
                      std::span<const std::uint8_t> rainbow_colors, bool visible)
{
    // Speech temporarily blanks this strip in both title and gameplay.
    // Clear it explicitly: the underlying title frame still holds its text.
    if (!visible) {
        std::fill(image.begin() + 192 * 320, image.end(), 0);
        return;
    }
    if (rainbow.size() != 24 || rainbow_colors.size() != 8)
        throw std::invalid_argument("Invalid footer graphic size");
    IndexedImage foreground{};
    const auto characters = render_characters(frame, &foreground);
    const unsigned scroll = phase & 7;
    const unsigned position = (phase - 952U) & 1023U;
    const int rainbow_x = static_cast<int>(position) - 24;
    std::fill_n(image.begin() + 192 * 320, 320, 0);
    // Seven glyph rows occupy display rows 193..199. The clipped bottom row
    // is part of the footer layout, not a scaling defect; do not shift it up.
    for (unsigned row = 0; row < 7; ++row) {
        for (unsigned x = 0; x < 320; ++x) {
            const auto target = (193 + row) * 320 + x;
            image[target] = 0;
            if (x < 7 || x >= 311) continue;
            const auto source = (192 + row) * 320 + x - scroll;
            image[target] = characters[source];
            const int bit = static_cast<int>(x) - rainbow_x;
            if (position < 512 && bit >= 0 && bit < 24 && !foreground[source] &&
                (rainbow[row * 3 + bit / 8] & (0x80U >> (bit % 8)))) {
                image[target] = row == 0 ? 2 : rainbow_colors[row - 1] & 15;
            }
        }
    }
}

IndexedImage render_title(const CharacterFrame& frame, std::uint8_t fine_scroll,
                          std::uint8_t text_phase)
{
    const auto source = render_characters(frame);
    auto result = source;
    // TitleSequence shifts the character rows when this phase wraps 0 -> 7.
    // Moving their pixels down by the remaining phase makes both ordinary
    // decrements and that row handoff advance the text upward by one pixel.
    // Keep the logo, ball and separately composited footer outside this window.
    const unsigned lyric_shift = text_phase & 7;
    for (unsigned y = 152; y < 192; ++y) {
        std::copy_n(source.begin() + (y - lyric_shift) * 320, 320,
                    result.begin() + y * 320);
    }
    const unsigned scroll = fine_scroll & 7;
    for (unsigned y = 192; y < 200; ++y) {
        for (unsigned x = 0; x < 320; ++x) {
            result[y * 320 + x] = (x >= 8 && x < 312)
                ? source[y * 320 + x - scroll] : frame.background & 15;
        }
    }
    return result;
}

} // namespace ghostbusters::video
