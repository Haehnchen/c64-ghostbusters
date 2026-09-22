#include "game/building_return.hpp"

#include "assets/capture_data.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {

BuildingReturn::BuildingReturn(const assets::Payload& payload, DriveControlsState state,
                               BuildingControlsRegisters registers)
    : payload_(payload), state_(std::move(state)), registers_(registers)
{
    if (state_.city.state3a != 0x20 && state_.city.state3a != 0x21) {
        throw std::invalid_argument("BuildingReturn requires State $20 or $21");
    }
    const assets::CaptureData capture_data(payload);
    scene_data_.assign(capture_data.scene_graphics().begin(),
                       capture_data.scene_graphics().end());
}

BuildingReturn::BuildingReturn(const assets::Payload& payload,
                               const BuildingControls& controls)
    : BuildingReturn(payload, controls.state(), controls.registers())
{
}

void BuildingReturn::move_sprite(std::size_t sprite)
{
    auto approach = [](std::uint8_t& value, std::uint8_t target) {
        if (value < target) ++value;
        else if (value > target) --value;
    };
    auto& sprites = state_.city.sprites;
    approach(sprites.x[sprite], sprites.target_x[sprite]);
    approach(sprites.y[sprite], sprites.target_y[sprite]);
}

void BuildingReturn::move_all_sprites()
{
    // $9AE3 invokes $9C25 with X=$0E,$0C,...,$00: sprites 7 down to 0.
    for (std::size_t sprite = 8; sprite-- > 0;) move_sprite(sprite);
}

void BuildingReturn::set_pointer(std::size_t sprite, std::uint8_t pointer)
{
    auto& sprites = state_.city.sprites;
    sprites.pointers[sprite] = pointer;
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) {
        throw std::out_of_range("building-return sprite pointer");
    }
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                sprites.bitmap_data[sprite].begin());
}

void BuildingReturn::animate_pair(std::size_t sprite, std::size_t animation,
                                  std::uint8_t settled_pointer, std::uint8_t frame)
{
    auto& sprites = state_.city.sprites;
    auto& value = registers_.animation77[animation];

    // $9B44 changes the animation phase only on even $09 frames.  The ROL
    // after SEC/SBC preserves the original carry as the horizontal facing.
    if ((frame & 1U) == 0) {
        auto direction = static_cast<std::uint8_t>(value & 1U);
        if (sprites.target_x[sprite] != sprites.x[sprite]) {
            direction = sprites.x[sprite] >= sprites.target_x[sprite] ? 1U : 0U;
        }
        value = static_cast<std::uint8_t>(value + 2U);
        if (value >= 8U) value = 0;
        value = static_cast<std::uint8_t>((value & 0xFEU) | direction);
    }

    if (sprites.x[sprite] == sprites.target_x[sprite] &&
        sprites.y[sprite] == sprites.target_y[sprite]) {
        set_pointer(sprite, static_cast<std::uint8_t>((value & 1U) | settled_pointer));
    } else {
        set_pointer(sprite, static_cast<std::uint8_t>(value + 0x0EU));
    }
}

void BuildingReturn::animate_both(std::uint8_t settled_pointer, std::uint8_t frame)
{
    // $9B35 processes sprite 6/$78 before sprite 5/$77.
    animate_pair(6, 1, settled_pointer, frame);
    animate_pair(5, 0, settled_pointer, frame);
}

bool BuildingReturn::sprite_at_target(std::size_t sprite) const noexcept
{
    const auto& sprites = state_.city.sprites;
    return sprites.x[sprite] == sprites.target_x[sprite] &&
           sprites.y[sprite] == sprites.target_y[sprite];
}

void BuildingReturn::position_outer_pair()
{
    // $9A16 follows sprite 5 with sprite 7, using the retained facing bit.
    auto& sprites = state_.city.sprites;
    const auto right = (registers_.animation77[0] & 1U) != 0;
    const auto x = static_cast<std::uint8_t>(sprites.x[5] + (right ? -6 : 6));
    const auto y = static_cast<std::uint8_t>(sprites.y[5] - 7U);
    sprites.x[7] = sprites.target_x[7] = x;
    sprites.y[7] = sprites.target_y[7] = y;
}

void BuildingReturn::tick(BuildingReturnInput input)
{
    auto& city = state_.city;
    auto& sprites = city.sprites;

    switch (city.state3a) {
    case 0x20:
        // $8875: animate with the settled buster base, move every sprite,
        // then test only sprite 5 at $9A01.
        animate_both(0x0E, input.frame09);
        move_all_sprites();
        if (sprite_at_target(5)) {
            // $8883-$888A stores A8/C7 into target pairs 5 and 6.
            sprites.target_x[5] = 0xA8;
            sprites.target_x[6] = 0xA8;
            sprites.target_y[5] = 0xC7;
            sprites.target_y[6] = 0xC7;
            // $8D86 clears $17 and advances the handler table state.
            city.key17 = 0;
            city.state3a = 0x21;
        }
        return;

    case 0x21:
        // $8896: this handler has no joystick, random, key, effect or music
        // input.  It only advances the busters toward their targets.
        animate_both(0x00, input.frame09);
        move_all_sprites();
        position_outer_pair();
        if (sprite_at_target(5) && sprite_at_target(6)) city.state3a = 0x11;
        return;

    default:
        return;
    }
}

} // namespace ghostbusters::game
