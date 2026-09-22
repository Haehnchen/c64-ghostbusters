#include "game/building_controls.hpp"

#include "game/title_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {

BuildingControls::BuildingControls(const assets::Payload& payload,
                                   DriveControlsState state,
                                   BuildingControlsRegisters registers)
    : capture_data_(payload), state_(std::move(state)), registers_(registers)
{
    if (state_.city.state3a < 0x18 || state_.city.state3a > 0x1A) {
        throw std::invalid_argument("BuildingControls requires State $18, $19 or $1A");
    }
    if (state_.city.current_building6e >= state_.city.building_status_c8.size()) {
        throw std::out_of_range("BuildingControls building index must be in 0..19");
    }
    scene_data_.assign(capture_data_.scene_graphics().begin(),
                       capture_data_.scene_graphics().end());
}

void BuildingControls::move_sprite(std::size_t sprite)
{
    auto approach = [](std::uint8_t& value, std::uint8_t target) {
        if (value < target) ++value;
        else if (value > target) --value;
    };
    auto& sprites = state_.city.sprites;
    approach(sprites.x[sprite], sprites.target_x[sprite]);
    approach(sprites.y[sprite], sprites.target_y[sprite]);
}

void BuildingControls::move_all_sprites()
{
    // $9AE3 walks offsets $0E,$0C,...,$00: sprites 7 down to 0.
    for (std::size_t sprite = 8; sprite-- > 0;) move_sprite(sprite);
}

void BuildingControls::move_control_target(std::size_t sprite, std::uint8_t joystick)
{
    auto& sprites = state_.city.sprites;
    if ((joystick & 0x08U) == 0) ++sprites.target_x[sprite];
    if ((joystick & 0x04U) == 0) --sprites.target_x[sprite];
    if ((joystick & 0x02U) == 0) ++sprites.target_y[sprite];
    if ((joystick & 0x01U) == 0) --sprites.target_y[sprite];
}

void BuildingControls::clamp_control_target(std::size_t sprite)
{
    auto& sprites = state_.city.sprites;
    auto& x = sprites.target_x[sprite];
    auto& y = sprites.target_y[sprite];
    if (x < 0x20) x = 0x20;
    if (x >= 0x8C) x = 0x8C;
    if (y < 0xAA) y = 0xAA;
    if (y >= 0xC8) y = 0xC8;
}

void BuildingControls::set_pointer(std::size_t sprite, std::uint8_t pointer)
{
    auto& sprites = state_.city.sprites;
    sprites.pointers[sprite] = pointer;
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) {
        throw std::out_of_range("building-controls sprite pointer");
    }
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                sprites.bitmap_data[sprite].begin());
}

void BuildingControls::animate_pair(std::size_t sprite, std::size_t animation,
                                    std::uint8_t settled_pointer, std::uint8_t frame)
{
    auto& sprites = state_.city.sprites;
    auto& value = registers_.animation77[animation];
    if ((frame & 1U) == 0) {
        auto direction = static_cast<std::uint8_t>(value & 1U);
        if (sprites.target_x[sprite] != sprites.x[sprite]) {
            // $9B50 subtracts target from current; ROL retains that SBC carry.
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

void BuildingControls::animate_both(std::uint8_t settled_pointer, std::uint8_t frame)
{
    // $9B35 invokes $9B44 first for sprite 6/$78 and then sprite 5/$77.
    animate_pair(6, 1, settled_pointer, frame);
    animate_pair(5, 0, settled_pointer, frame);
}

void BuildingControls::position_outer_pair()
{
    auto& sprites = state_.city.sprites;
    const auto right = (registers_.animation77[0] & 1U) != 0;
    const auto x = static_cast<std::uint8_t>(sprites.x[5] + (right ? -6 : 6));
    const auto y = static_cast<std::uint8_t>(sprites.y[5] - 7U);
    sprites.x[7] = sprites.target_x[7] = x;
    sprites.y[7] = sprites.target_y[7] = y;
}

void BuildingControls::update_ghost(BuildingControlsInput input)
{
    auto& city = state_.city;
    auto& sprites = city.sprites;
    sprites.priority_mask = static_cast<std::uint8_t>(sprites.priority_mask & 0xEFU);
    if ((city.owned_mask6d & 0x02U) == 0) {
        sprites.priority_mask = static_cast<std::uint8_t>(sprites.priority_mask | 0x10U);
    }

    const auto direction = static_cast<std::uint16_t>(registers_.direction7d);
    const auto delta = capture_data_.ghost_delta(static_cast<std::uint8_t>(direction));
    sprites.x[4] = static_cast<std::uint8_t>(
        sprites.x[4] + delta[0]);
    sprites.y[4] = static_cast<std::uint8_t>(
        sprites.y[4] + delta[1]);
    std::uint8_t turn = 0;
    if (sprites.y[4] >= 0x70) {
        sprites.y[4] = 0x6E;
        turn = 4;
    }
    if (sprites.y[4] < 0x30) {
        sprites.y[4] = 0x32;
        turn = 4;
    }
    if (sprites.x[4] >= 0x90) {
        sprites.x[4] = 0x8E;
        turn = 4;
    }
    if (sprites.x[4] < 0x48) {
        sprites.x[4] = 0x4A;
        turn = 4;
    }
    sprites.target_x[4] = sprites.x[4];
    sprites.target_y[4] = sprites.y[4];
    registers_.direction7d = static_cast<std::uint8_t>((registers_.direction7d + turn) & 7U);

    if ((input.frame09 & 3U) == 0) {
        registers_.direction7d = static_cast<std::uint8_t>(
            (registers_.direction7d + (input.random06 & 1U)) & 7U);
        set_pointer(4, static_cast<std::uint8_t>((input.random06 & 1U) + 0x0AU));
    }
}

bool BuildingControls::check_timeout_and_cleanup()
{
    auto& city = state_.city;
    if ((city.building_status_c8[city.current_building6e] & 0x0CU) == 0x0CU) {
        return false;
    }
    set_pointer(4, 0);
    if (city.countdown7c != 0) return false;

    // $9A8A discards its caller and enters State $1F's $8823 continuation.
    auto& sprites = city.sprites;
    city.state3a = 0x20;
    sprites.target_x[5] = sprites.x[7];
    sprites.target_x[6] = static_cast<std::uint8_t>(sprites.x[7] + 0x12U);
    sprites.target_y[5] = sprites.y[7];
    sprites.target_y[6] = sprites.y[7];
    city.key17 = 0;
    return true;
}

bool BuildingControls::fire_press(std::uint8_t joystick)
{
    const auto fire = static_cast<std::uint8_t>(joystick & 0x10U);
    const bool changed = fire != state_.city.fire_latch11;
    state_.city.fire_latch11 = fire;
    return changed && fire == 0;
}

void BuildingControls::prepare_beam_directions()
{
    const auto& sprites = state_.city.sprites;
    std::array<std::uint8_t, 2> beam_x{};
    for (std::size_t i = 0; i < 2; ++i) {
        const auto sprite = 5U + i;
        auto delta = static_cast<std::uint8_t>(sprites.y[sprite] - sprites.y[4]);
        delta = static_cast<std::uint8_t>(delta >> 2U);
        const auto pointer_delta = static_cast<std::uint8_t>(
            sprites.pointers[sprite] - 0x0EU);
        if ((pointer_delta & 1U) != 0) {
            delta = static_cast<std::uint8_t>(0U - delta);
        }
        beam_x[i] = static_cast<std::uint8_t>(sprites.x[sprite] + delta);
    }
    for (std::size_t i = 0; i < 2; ++i) {
        registers_.beam_direction7e[i] = sprites.x[4] >= beam_x[i] ? 0x00 : 0xFF;
    }
}

void BuildingControls::tick(BuildingControlsInput input)
{
    auto& city = state_.city;
    auto& sprites = city.sprites;
    switch (city.state3a) {
    case 0x18:
        animate_both(0x0E, input.frame09);
        move_all_sprites();
        move_control_target(5, input.joystick33);
        clamp_control_target(5);
        position_outer_pair();
        if ((city.building_status_c8[city.current_building6e] & 0x0CU) == 0x0CU &&
            fire_press(input.joystick33)) {
            sprites.target_y[7] = static_cast<std::uint8_t>(sprites.y[5] + 1U);
            city.key17 = 0;
            ++city.state3a;
            return;
        }
        update_ghost(input);
        (void)check_timeout_and_cleanup();
        return;

    case 0x19:
        move_all_sprites();
        update_ghost(input);
        if (check_timeout_and_cleanup()) return;
        move_control_target(5, input.joystick33);
        clamp_control_target(5);
        animate_both(0x0E, input.frame09);
        if (fire_press(input.joystick33)) {
            set_pointer(5, static_cast<std::uint8_t>((sprites.pointers[5] & 1U) | 0x16U));
            const auto target = capture_data_.second_buster_target();
            sprites.target_x[6] = target[0];
            sprites.target_y[6] = target[1];
            city.key17 = 0;
            ++city.state3a;
        }
        return;

    case 0x1A:
        move_all_sprites();
        update_ghost(input);
        if (check_timeout_and_cleanup()) return;
        move_control_target(6, input.joystick33);
        clamp_control_target(6);
        animate_pair(6, 1, 0x0E, input.frame09);
        if (fire_press(input.joystick33)) {
            set_pointer(6, static_cast<std::uint8_t>((sprites.pointers[6] & 1U) | 0x16U));
            prepare_beam_directions();
            city.key17 = 0;
            ++city.state3a;
        }
        return;

    default:
        return;
    }
}

} // namespace ghostbusters::game
