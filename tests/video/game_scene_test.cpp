#include "video/game_scene.hpp"

#include <iostream>
#include <stdexcept>

using namespace ghostbusters::video;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try {
        CharacterFrame characters;
        characters.background = 2;
        characters.multicolor = false;
        characters.colors[0] = 5;
        characters.charset[0] = 0x80;

        SpriteLayer sprites;
        sprites.sprites[0].enabled = true;
        sprites.sprites[0].x = 20;
        sprites.sprites[0].y = 2;
        sprites.sprites[0].color = 6;
        sprites.sprites[0].bitmap[0] = 0x80;
        sprites.sprites[1].enabled = true;
        sprites.sprites[1].x = 0;
        sprites.sprites[1].y = 2;
        sprites.sprites[1].color = 4;
        sprites.sprites[1].bitmap[0] = 0x80;
        sprites.sprites[2] = sprites.sprites[1];
        sprites.sprites[2].x = 310;
        sprites.sprites[3] = sprites.sprites[1];
        sprites.sprites[3].x = 7;
        sprites.sprites[3].y = 1;
        sprites.sprites[3].behind_foreground = true;

        const auto image = render_game_scene(characters, sprites);
        require(image[1U * 320U + 7U] == 5,
                "shifted character and foreground mask put the first glyph at (7,1)");
        require(image[2U * 320U + 20] == 6,
                "game-scene character alignment must not move VIC-relative sprites");
        require(image[2U * 320U] == 0 && image[2U * 320U + 310] == 4 && image[2U * 320U + 311] == 0,
                "38-column border exposes exactly viewport X 7 through 310");
        require(image[2U * 320U + 7] == 2 && image[2U * 320U + 6] == 0,
                "the left border ends immediately before viewport X 7");
        for (unsigned x = 0; x < 320; ++x)
            require(image[x] == 0, "raster50 remains upper border before the first character row");
        GameSceneOptions open_border;
        open_border.horizontal_scroll = 0;
        open_border.horizontal_border = false;
        const auto unmasked = render_game_scene(characters, sprites, open_border);
        require(unmasked[1U * 320U] == 5,
                "horizontal-scroll override uses the low D016 fine-scroll value");
        require(unmasked[2U * 320U] == 4 && unmasked[2U * 320U + 310] == 4,
                "disabling the horizontal border leaves edge sprites visible");

        // Exercise every fine-scroll phase: the scene color must not change
        // glyphs, foreground masking, the upper playfield or the lower border.
        CharacterFrame notice;
        notice.background = 2;
        notice.multicolor = false;
        notice.colors.fill(5);
        notice.charset[0] = 0x80;
        for (unsigned phase = 0; phase < 8; ++phase) {
            GameSceneOptions options;
            options.notice_phase = static_cast<std::uint8_t>(phase);
            options.split_band = true;
            const auto blue = render_game_scene(notice, {}, options);
            options.notice_background = 0;
            const auto black = render_game_scene(notice, {}, options);
            for (unsigned y = 0; y < 200; ++y) {
                for (unsigned x = 0; x < 320; ++x) {
                    const auto i = y * 320 + x;
                    const bool band = y >= 177 && y < 185 && x >= 7 && x < 311;
                    require(black[i] == (band && blue[i] == 6 ? 0 : blue[i]),
                            "rooftop changes only notice background, at every scroll phase");
                }
            }
            require(blue[178U * 320 + 100] == 6 && black[178U * 320 + 100] == 0,
                    "normal notice is blue, rooftop notice is black");
        }

        std::cout << "game scene alignment tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
