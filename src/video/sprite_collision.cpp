#include "video/sprite_collision.hpp"

namespace ghostbusters::video {
namespace {

constexpr int kSpriteWidth = 24;
constexpr int kSpriteHeight = 21;

[[nodiscard]] bool base_pixel_opaque(const Sprite& sprite, int x, int y)
{
    if (sprite.multicolor) {
        const int code = x / 2;
        const auto data = sprite.bitmap[static_cast<std::size_t>(y * 3 + code / 4)];
        const int shift = 6 - (code % 4) * 2;
        return ((data >> shift) & 0x03) != 0;
    }

    const auto data = sprite.bitmap[static_cast<std::size_t>(y * 3 + x / 8)];
    return (data & (static_cast<std::uint8_t>(0x80) >> (x % 8))) != 0;
}

[[nodiscard]] bool pixel_opaque_at(const Sprite& sprite, int x, int y)
{
    const int scale_x = sprite.expand_x ? 2 : 1;
    const int scale_y = sprite.expand_y ? 2 : 1;
    const int local_x = x - static_cast<int>(sprite.x);
    const int local_y = y - static_cast<int>(sprite.y);
    const int width = kSpriteWidth * scale_x;
    const int height = kSpriteHeight * scale_y;
    if (local_x < 0 || local_x >= width || local_y < 0 || local_y >= height)
        return false;
    return base_pixel_opaque(sprite, local_x / scale_x, local_y / scale_y);
}

[[nodiscard]] bool sprites_collide(const Sprite& first, const Sprite& second)
{
    const int first_scale_x = first.expand_x ? 2 : 1;
    const int first_scale_y = first.expand_y ? 2 : 1;
    const int second_scale_x = second.expand_x ? 2 : 1;
    const int second_scale_y = second.expand_y ? 2 : 1;
    const int first_width = kSpriteWidth * first_scale_x;
    const int first_height = kSpriteHeight * first_scale_y;
    const int second_width = kSpriteWidth * second_scale_x;
    const int second_height = kSpriteHeight * second_scale_y;

    const int first_left = static_cast<int>(first.x);
    const int first_top = static_cast<int>(first.y);
    const int second_left = static_cast<int>(second.x);
    const int second_top = static_cast<int>(second.y);
    if (first_left + first_width <= second_left ||
        second_left + second_width <= first_left ||
        first_top + first_height <= second_top ||
        second_top + second_height <= first_top)
        return false;

    // Sample first's expanded opaque pixels and ask whether the same logical
    // coordinate is opaque in second. No visible-screen clipping is done.
    for (int base_y = 0; base_y < kSpriteHeight; ++base_y) {
        for (int base_x = 0; base_x < kSpriteWidth; ++base_x) {
            if (!base_pixel_opaque(first, base_x, base_y))
                continue;
            for (int dy = 0; dy < first_scale_y; ++dy) {
                const int y = first_top + base_y * first_scale_y + dy;
                for (int dx = 0; dx < first_scale_x; ++dx) {
                    const int x = first_left + base_x * first_scale_x + dx;
                    if (pixel_opaque_at(second, x, y))
                        return true;
                }
            }
        }
    }
    return false;
}

} // namespace

std::uint8_t sprite_collision_mask(const SpriteLayer& layer)
{
    std::uint8_t result = 0;
    for (std::size_t first = 0; first < layer.sprites.size(); ++first) {
        if (!layer.sprites[first].enabled)
            continue;
        for (std::size_t second = first + 1; second < layer.sprites.size(); ++second) {
            if (!layer.sprites[second].enabled ||
                !sprites_collide(layer.sprites[first], layer.sprites[second]))
                continue;
            result = static_cast<std::uint8_t>(result | (1u << first) | (1u << second));
        }
    }
    return result;
}

} // namespace ghostbusters::video
