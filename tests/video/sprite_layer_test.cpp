#include "video/sprite_layer.hpp"

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
        IndexedImage image{};
        SpriteLayer layer;
        auto& sprite = layer.sprites[0];
        sprite.enabled = true;
        sprite.color = 5;
        sprite.bitmap[0] = 0x81;
        sprite.bitmap[62] = 1;
        composite_sprites(image, layer);
        require(image[0] == 5 && image[7] == 5 && image[1] == 0 && image[20 * 320 + 23] == 5,
                "Hires sprite bit order and last row");
        image.fill(9);
        sprite.bitmap.fill(0);
        sprite.bitmap[0] = 0x1B; // transparent, shared1, private, shared2
        sprite.multicolor = true;
        sprite.expand_x = sprite.expand_y = true;
        layer.multicolor1 = 2;
        layer.multicolor2 = 3;
        composite_sprites(image, layer);
        require(image[0] == 9 && image[3] == 9 && image[4] == 2 && image[7] == 2 &&
                    image[8] == 5 && image[11] == 5 && image[12] == 3 && image[15] == 3 &&
                    image[320 + 12] == 3 && image[640 + 12] == 9,
                "Multicolor mapping, transparency and both expansions");
        sprite.x = -15;
        sprite.y = 199;
        image.fill(9);
        composite_sprites(image, layer);
        require(image[199 * 320] == 3 && image[199 * 320 + 1] == 9,
                "Sprites clip safely against image boundaries");
        sprite.x = sprite.y = 0;
        sprite.multicolor = sprite.expand_x = sprite.expand_y = false;
        sprite.bitmap[0] = 0x80;
        sprite.behind_foreground = true;
        layer.sprites[1] = sprite;
        layer.sprites[1].behind_foreground = false;
        layer.sprites[1].color = 6;
        IndexedImage foreground{};
        foreground[0] = 1;
        image.fill(9);
        composite_sprites(image, layer, foreground);
        require(image[0] == 9, "Foreground hides winning sprite, not exposing lower-priority sprite");
        foreground[0] = 0;
        composite_sprites(image, layer, foreground);
        require(image[0] == 5, "Lower sprite number wins overlap");
        std::cout << "Static sprite composition passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
