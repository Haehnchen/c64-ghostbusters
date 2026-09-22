#include "game/title_screen.hpp"
#include "assets/ui_data.hpp"

#include <algorithm>
#include <stdexcept>

namespace ghostbusters::game {

std::vector<std::uint8_t> shared_sprite_data(const assets::Payload& payload)
{
    const auto graphics = payload.asset("graphics/shared_sprites");
    if (graphics.size() != 59 * 64) {
        throw std::runtime_error("Expected 59 shared sprite records");
    }
    return {graphics.begin(), graphics.end()};
}

TitleScreen prepare_title_screen(const assets::Payload& payload,
                                std::span<const std::uint8_t> font)
{
    if (font.size() != 4096) {
        throw std::runtime_error("Expected 4096 native font glyph bytes");
    }
    TitleScreen title;
    const assets::UiData data(payload);
    auto& frame = title.characters;
    title.scene_data = shared_sprite_data(payload);
    const auto rainbow = payload.asset("ui/footer_rainbow");
    if (rainbow.size() != title.rainbow.size())
        throw std::runtime_error("Unexpected footer rainbow size");
    std::ranges::copy(rainbow, title.rainbow.begin());

    std::ranges::copy(font.subspan(8, 1016), frame.charset.begin() + 8);
    std::ranges::copy(data.font_patch(), frame.charset.begin() + 0xD8);
    std::copy_n(frame.charset.begin() + 0x78, 8, frame.charset.begin() + 0x180);
    std::ranges::copy(data.title_charset(), frame.charset.begin() + 0x200);

    frame.screen.fill(0);
    frame.colors.fill(9);
    std::fill_n(frame.colors.begin() + 0x50, 0x79, 10);
    for (std::size_t row = 0; row < 3; ++row) {
        std::ranges::copy(data.header_row(row),
                          frame.screen.begin() + 0x55 + row * 40);
    }
    for (std::size_t row = 0; row < 12; ++row) {
        std::ranges::copy(data.logo_row(row),
                          frame.screen.begin() + 0xFB + row * 40);
    }
    std::fill_n(frame.colors.begin() + 0x320, 40, 1);
    std::fill_n(frame.colors.begin() + 0x3C0, 40, 1);
    std::fill_n(frame.colors.begin() + 0x348, 80, 2);
    // The sprite pointer lives in the unused tail of screen memory.
    frame.screen[0x3F8] = 0x0D;
    frame.background = 0;
    frame.multicolor1 = 1;
    frame.multicolor2 = 2;
    frame.multicolor = true;
    return title;
}

} // namespace ghostbusters::game
