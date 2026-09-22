#include "video/sprite_collision.hpp"

#include <iostream>
#include <stdexcept>

using namespace ghostbusters::video;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void set_hires_pixel(Sprite& sprite, int x, int y)
{
    sprite.bitmap[static_cast<std::size_t>(y * 3 + x / 8)] |=
        static_cast<std::uint8_t>(0x80 >> (x % 8));
}

void set_multicolor_code(Sprite& sprite, int code_x, int y, std::uint8_t code)
{
    const auto index = static_cast<std::size_t>(y * 3 + code_x / 4);
    const int shift = 6 - (code_x % 4) * 2;
    sprite.bitmap[index] = static_cast<std::uint8_t>(
        sprite.bitmap[index] | ((code & 0x03) << shift));
}

int main()
{
    try {
        SpriteLayer layer;
        auto& first = layer.sprites[0];
        auto& second = layer.sprites[1];
        first.enabled = second.enabled = true;
        first.x = second.x = 40;
        first.y = second.y = 30;
        set_hires_pixel(first, 0, 0);
        // Their full sprite bounds overlap, but their only opaque pixels do not.
        set_hires_pixel(second, 1, 0);
        require(sprite_collision_mask(layer) == 0,
                "Transparent pixels must not trigger a collision");

        layer = {};
        auto& multicolor = layer.sprites[0];
        auto& target = layer.sprites[1];
        multicolor.enabled = target.enabled = true;
        multicolor.multicolor = true;
        multicolor.x = 100;
        multicolor.y = 80;
        target.x = 104;
        target.y = 80;
        // Code 1 occupies two horizontal pixels, including when its palette
        // color would happen to be zero.
        set_multicolor_code(multicolor, 2, 0, 1);
        set_hires_pixel(target, 0, 0);
        require(sprite_collision_mask(layer) == 0b00000011,
                "Multicolor codes must occupy two horizontal pixels");

        layer = {};
        auto& expanded = layer.sprites[0];
        auto& expanded_target = layer.sprites[1];
        expanded.enabled = expanded_target.enabled = true;
        expanded.expand_x = expanded.expand_y = true;
        expanded.x = 10;
        expanded.y = 12;
        set_hires_pixel(expanded, 3, 4);
        expanded_target.x = 16;
        expanded_target.y = 20;
        set_hires_pixel(expanded_target, 0, 0);
        require(sprite_collision_mask(layer) == 0b00000011,
                "X and Y expansion must enlarge opaque pixels");

        expanded_target.enabled = false;
        require(sprite_collision_mask(layer) == 0,
                "Disabled sprites must not participate in collisions");

        layer = {};
        for (auto& sprite : layer.sprites)
            sprite.enabled = true;
        for (auto& sprite : layer.sprites)
            set_hires_pixel(sprite, 0, 0);
        require(sprite_collision_mask(layer) == 0xFF,
                "All colliding sprites must be represented in the mask");

        layer = {};
        auto& negative_first = layer.sprites[0];
        auto& negative_second = layer.sprites[1];
        negative_first.enabled = negative_second.enabled = true;
        negative_first.x = -400;
        negative_first.y = -250;
        negative_second.x = -400;
        negative_second.y = -250;
        set_hires_pixel(negative_first, 0, 0);
        set_hires_pixel(negative_second, 0, 0);
        require(sprite_collision_mask(layer) == 0b00000011,
                "Negative coordinates must remain valid geometry");

        std::cout << "Sprite collision tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
