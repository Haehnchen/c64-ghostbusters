#include "platform/sdl/scene_view.hpp"
#include <algorithm>

namespace ghostbusters::platform::sdl {
ghostbusters::video::SpriteLayer scene_sprites(const ghostbusters::game::EquipmentSpriteState& state)
{
    ghostbusters::video::SpriteLayer layer;
    layer.multicolor1 = state.shared_multicolor_1;
    layer.multicolor2 = state.shared_multicolor_2;
    for (unsigned i = 0; i < 8; ++i) {
        auto& sprite = layer.sprites[i];
        std::copy_n(state.bitmap_data[i].begin(), 63, sprite.bitmap.begin());
        // $8F83-$8F90 converts the game's half-X coordinates to VIC positions.
        sprite.x = static_cast<std::int16_t>(state.x[i] * 2 + 1 - 24);
        // VIC compares Y before fetching: the first bitmap row appears at Y+1.
        sprite.y = static_cast<std::int16_t>(state.y[i] + 1 - 50);
        sprite.color = state.colors[i];
        sprite.enabled = (state.enabled_mask & (1U << i)) != 0;
        sprite.multicolor = (state.multicolor_mask & (1U << i)) != 0;
        sprite.expand_x = (state.x_expand_mask & (1U << i)) != 0;
        sprite.expand_y = (state.y_expand_mask & (1U << i)) != 0;
        sprite.behind_foreground = (state.priority_mask & (1U << i)) != 0;
    }
    return layer;
}

ghostbusters::video::IndexedImage scene_image(const ghostbusters::video::CharacterFrame& characters,
                                                const ghostbusters::game::EquipmentSpriteState& state,
                                                std::optional<std::uint8_t> notice_phase,
                                                std::optional<std::uint8_t> drive_scroll,
                                                bool split_band,
                                                std::uint8_t notice_background)
{
    ghostbusters::video::GameSceneOptions options;
    options.horizontal_scroll = drive_scroll.value_or(7) & 7U;
    options.notice_phase = notice_phase;
    options.notice_background = notice_background;
    options.split_band = drive_scroll.has_value() || split_band;
    options.horizontal_border = (drive_scroll.value_or(0x17) & 8U) == 0;
    return ghostbusters::video::render_game_scene(characters, scene_sprites(state), options);
}

ghostbusters::video::IndexedImage equipment_image(const ghostbusters::game::EquipmentSelection& equipment)
{
    auto image = scene_image(equipment.characters(), equipment.sprites());
    std::fill(image.begin() + 186 * 320, image.end(), 0);
    return image;
}

} // namespace ghostbusters::platform::sdl
