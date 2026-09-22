#include "game/title_scroller.hpp"
#include "assets/ui_data.hpp"

#include <cstddef>

namespace ghostbusters::game {
namespace {

constexpr std::uint16_t kPhaseMask = 0x03FF;
constexpr std::size_t kTextLength = 128;
constexpr std::size_t kRowStart = 960;
constexpr std::size_t kRowLength = 40;

std::uint8_t screenCode(std::uint8_t value) noexcept
{
    // The original routine turns PETSCII spaces into blank glyphs and maps
    // uppercase/graphics bytes to the lower six-bit character code.
    if (value == 0x20) return 0;
    if (value >= 0x40) return static_cast<std::uint8_t>(value & 0x3F);
    return value;
}

} // namespace

void TitleScroller::tick(std::uint8_t control)
{
    phase &= kPhaseMask;
    if ((control & 0xC0) != 0x80) {
        phase = static_cast<std::uint16_t>((phase - 1) & kPhaseMask);
    }
}

std::uint8_t TitleScroller::fine_scroll() const
{
    return static_cast<std::uint8_t>(phase & 0x07);
}

std::uint8_t TitleScroller::source_index() const
{
    return static_cast<std::uint8_t>((~(phase >> 3)) & 0x7F);
}

void TitleScroller::write_row(const assets::Payload& payload,
                               video::CharacterFrame& frame) const
{
    const auto text = assets::UiData(payload).scroller_text();
    const std::size_t first = source_index();
    for (std::size_t column = 0; column < kRowLength; ++column) {
        const std::size_t source = (first + column) & (kTextLength - 1);
        frame.screen[kRowStart + column] = screenCode(text[source]);
    }
}

} // namespace ghostbusters::game
