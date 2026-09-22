#include "video/character_frame.hpp"

#include <cstddef>

namespace ghostbusters::video {
namespace {

constexpr std::size_t kColumns = 40;
constexpr std::size_t kRows = 25;
constexpr std::size_t kCellWidth = 8;
constexpr std::size_t kCellHeight = 8;

std::uint8_t hiresColor(const CharacterFrame& frame, std::uint8_t cellColor,
                        bool foreground) noexcept
{
    return foreground ? static_cast<std::uint8_t>(cellColor & 0x0F)
                      : static_cast<std::uint8_t>(frame.background & 0x0F);
}

std::uint8_t multicolorColor(const CharacterFrame& frame, std::uint8_t cellColor,
                             std::uint8_t pair) noexcept
{
    switch (pair) {
    case 0:
        return static_cast<std::uint8_t>(frame.background & 0x0F);
    case 1:
        return static_cast<std::uint8_t>(frame.multicolor1 & 0x0F);
    case 2:
        return static_cast<std::uint8_t>(frame.multicolor2 & 0x0F);
    default:
        return static_cast<std::uint8_t>(cellColor & 0x07);
    }
}

} // namespace

IndexedImage render_characters(const CharacterFrame& frame, IndexedImage* foreground_mask)
{
    IndexedImage image{};

    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            const std::size_t cell = row * kColumns + column;
            const std::size_t glyphOffset =
                static_cast<std::size_t>(frame.screen[cell]) * kCellHeight;
            const std::uint8_t cellColor = frame.colors[cell];
            const bool cellMulticolor = frame.multicolor && (cellColor & 0x08) != 0;

            for (std::size_t glyphRow = 0; glyphRow < kCellHeight; ++glyphRow) {
                const std::uint8_t bits = frame.charset[glyphOffset + glyphRow];
                const std::size_t y = row * kCellHeight + glyphRow;
                const std::size_t x = column * kCellWidth;

                if (cellMulticolor) {
                    for (std::size_t pairIndex = 0; pairIndex < 4; ++pairIndex) {
                        const auto shift = static_cast<unsigned>(6 - pairIndex * 2);
                        const std::uint8_t pair = static_cast<std::uint8_t>((bits >> shift) & 0x03);
                        const std::uint8_t color = multicolorColor(frame, cellColor, pair);
                        const std::size_t pixel = x + pairIndex * 2;
                        image[y * 320 + pixel] = color;
                        image[y * 320 + pixel + 1] = color;
                        if (foreground_mask) {
                            (*foreground_mask)[y * 320 + pixel] = (pair & 2) != 0;
                            (*foreground_mask)[y * 320 + pixel + 1] = (pair & 2) != 0;
                        }
                    }
                } else {
                    for (std::size_t bitIndex = 0; bitIndex < kCellWidth; ++bitIndex) {
                        const bool foreground = (bits & (0x80 >> bitIndex)) != 0;
                        image[y * 320 + x + bitIndex] =
                            hiresColor(frame, cellColor, foreground);
                        if (foreground_mask) (*foreground_mask)[y * 320 + x + bitIndex] = foreground;
                    }
                }
            }
        }
    }

    return image;
}

} // namespace ghostbusters::video
