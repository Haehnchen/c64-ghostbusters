#include "video/game_scene.hpp"

#include <algorithm>

namespace ghostbusters::video {
namespace {

constexpr unsigned kWidth = 320;
constexpr unsigned kHeight = 200;
constexpr unsigned kCharacterY = 1;
constexpr unsigned kLeft38ColumnEdge = 7;
constexpr unsigned kRight38ColumnEdge = 311;

struct ShiftedCharacters {
    IndexedImage image{};
    IndexedImage foreground{};
};

ShiftedCharacters render_shifted_characters(const CharacterFrame& characters,
                                            std::uint8_t horizontal_scroll)
{
    ShiftedCharacters result;
    IndexedImage source_foreground{};
    const auto source = render_characters(characters, &source_foreground);
    result.image.fill(static_cast<std::uint8_t>(characters.background & 0x0F));

    const unsigned scroll = horizontal_scroll & 7U;
    for (unsigned source_y = 0; source_y + kCharacterY < kHeight; ++source_y) {
        const unsigned destination_y = source_y + kCharacterY;
        for (unsigned source_x = 0; source_x + scroll < kWidth; ++source_x) {
            const unsigned destination_x = source_x + scroll;
            const auto source_index = source_y * kWidth + source_x;
            const auto destination_index = destination_y * kWidth + destination_x;
            result.image[destination_index] = source[source_index];
            result.foreground[destination_index] = source_foreground[source_index];
        }
    }
    return result;
}

void mask_horizontal_border(IndexedImage& image, std::uint8_t color)
{
    for (unsigned y = 0; y < kHeight; ++y) {
        std::fill(image.begin() + y * kWidth,
                  image.begin() + y * kWidth + kLeft38ColumnEdge, color);
        std::fill(image.begin() + y * kWidth + kRight38ColumnEdge,
                  image.begin() + (y + 1U) * kWidth, color);
    }
}

} // namespace

IndexedImage render_game_scene(const CharacterFrame& characters,
                               const SpriteLayer& sprites,
                               GameSceneOptions options)
{
    auto main = render_shifted_characters(characters, options.horizontal_scroll);

    if (options.notice_phase) {
        auto lower_characters = characters;
        lower_characters.background = options.notice_background & 0x0F;
        const auto lower = render_shifted_characters(lower_characters, *options.notice_phase);
        // The character matrix starts at viewport Y=1, so screen row 22 spans
        // Y=177..184 rather than the renderer-local 176..183.
        constexpr unsigned first_notice_y = 177;
        constexpr unsigned past_notice_y = 185;
        std::copy(lower.image.begin() + first_notice_y * kWidth,
                  lower.image.begin() + past_notice_y * kWidth,
                  main.image.begin() + first_notice_y * kWidth);
        std::copy(lower.foreground.begin() + first_notice_y * kWidth,
                  lower.foreground.begin() + past_notice_y * kWidth,
                  main.foreground.begin() + first_notice_y * kWidth);
    }

    if (options.split_band) {
        const auto character_band = main.image;
        composite_sprites(main.image, sprites, main.foreground);
        // Retain the existing frame-level approximation of the IRQ split.
        // These raster boundaries are independent of character-matrix origin;
        // exact instruction-cycle split timing remains separate fidelity work.
        std::copy(character_band.begin() + 176U * kWidth, character_band.end(),
                  main.image.begin() + 176U * kWidth);
        std::fill(main.image.begin() + 186U * kWidth, main.image.end(), 0);
    } else {
        composite_sprites(main.image, sprites, main.foreground);
    }

    if (options.horizontal_border) {
        mask_horizontal_border(main.image,
                               static_cast<std::uint8_t>(options.border_color & 0x0F));
    }
    // D011=$1B opens the upper border at raster 51: viewport row 0
    // (raster 50) remains border, including over sprites.
    std::fill_n(main.image.begin(), kWidth, options.border_color & 0x0F);
    return main.image;
}

} // namespace ghostbusters::video
