#include "video/sprite_layer.hpp"

#include <stdexcept>

namespace ghostbusters::video {

void composite_sprites(IndexedImage& image, const SpriteLayer& layer,
                       std::span<const std::uint8_t> foreground)
{
    if (!foreground.empty() && foreground.size() != image.size())
        throw std::invalid_argument("Sprite foreground mask must cover 320x200 pixels");
    std::array<bool, 320 * 200> occupied{};
    // Lowest sprite number wins before the character-foreground decision.
    for (const auto& sprite : layer.sprites) {
        if (!sprite.enabled) continue;
        const int scale_x = sprite.expand_x ? 2 : 1;
        const int scale_y = sprite.expand_y ? 2 : 1;
        const int bits = sprite.multicolor ? 2 : 1;
        for (int row = 0; row < 21; ++row) {
            for (int bit = 0; bit < 24; bit += bits) {
                const auto data = sprite.bitmap[static_cast<std::size_t>(row * 3 + bit / 8)];
                const auto code = (data >> (8 - bits - bit % 8)) & ((1 << bits) - 1);
                if (code == 0) continue;
                const auto color = !sprite.multicolor || code == 2 ? sprite.color
                                   : code == 1 ? layer.multicolor1 : layer.multicolor2;
                for (int dy = 0; dy < scale_y; ++dy) {
                    const int y = sprite.y + row * scale_y + dy;
                    if (y < 0 || y >= 200) continue;
                    for (int dx = 0; dx < bits * scale_x; ++dx) {
                        const int x = sprite.x + bit * scale_x + dx;
                        if (x < 0 || x >= 320) continue;
                        const auto index = static_cast<std::size_t>(y * 320 + x);
                        if (occupied[index]) continue;
                        occupied[index] = true;
                        if (sprite.behind_foreground && !foreground.empty() && foreground[index]) continue;
                        image[index] = color & 0x0F;
                    }
                }
            }
        }
    }
}

} // namespace ghostbusters::video
