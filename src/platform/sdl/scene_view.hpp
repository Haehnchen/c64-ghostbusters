#pragma once
#include "game/equipment_selection.hpp"
#include "video/game_scene.hpp"
#include <array>
#include <optional>

namespace ghostbusters::platform::sdl {
// Pepto PAL palette, as shipped with VICE (C64/pepto-pal.vpl).
// This is a fixed display palette, not an emulation of analog PAL output.
inline constexpr std::array<std::uint32_t, 16> palette{
    0xFF000000, 0xFFFFFFFF, 0xFF68372B, 0xFF70A4B2,
    0xFF6F3D86, 0xFF588D43, 0xFF352879, 0xFFB8C76F,
    0xFF6F4F25, 0xFF433900, 0xFF9A6759, 0xFF444444,
    0xFF6C6C6C, 0xFF9AD284, 0xFF6C5EB5, 0xFF959595};


ghostbusters::video::SpriteLayer scene_sprites(const ghostbusters::game::EquipmentSpriteState& state);
ghostbusters::video::IndexedImage scene_image(
    const ghostbusters::video::CharacterFrame& characters,
    const ghostbusters::game::EquipmentSpriteState& state,
    std::optional<std::uint8_t> notice_phase = std::nullopt,
    std::optional<std::uint8_t> drive_scroll = std::nullopt,
    bool split_band = false, std::uint8_t notice_background = 6);
ghostbusters::video::IndexedImage equipment_image(const ghostbusters::game::EquipmentSelection& equipment);
} // namespace ghostbusters::platform::sdl
