#include "game/zuul.hpp"

#include "assets/zuul_data.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

[[nodiscard]] std::uint8_t bcd_add(std::uint8_t left, std::uint8_t right,
                                   bool& carry)
{
    unsigned low = (left & 0x0FU) + (right & 0x0FU) + (carry ? 1U : 0U);
    if (low > 9U) low += 6U;
    unsigned value = (left & 0xF0U) + (right & 0xF0U) + low;
    if (value > 0x99U) value += 0x60U;
    carry = value > 0xFFU;
    return static_cast<std::uint8_t>(value);
}

} // namespace

Zuul::Zuul(const assets::Payload& payload, DriveControlsState state,
           BuildingControlsRegisters building_registers,
           ZuulRegisters registers)
    : payload_(payload), state_(std::move(state)),
      building_registers_(building_registers), registers_(registers)
{
    if (state_.city.state3a < 0x28 || state_.city.state3a > 0x2C) {
        throw std::invalid_argument("Zuul requires State $28-$2C");
    }
    const assets::ZuulData data(payload_);
    const auto decoded = data.scene_graphics();
    scene_data_.assign(0x1100, 0);
    std::copy(decoded.begin(), decoded.end(), scene_data_.begin());

    data.mirror_sprite_sheets(scene_data_);
}

void Zuul::set_pointer(std::size_t sprite, std::uint8_t pointer)
{
    auto& sprites = state_.city.sprites;
    sprites.pointers[sprite] = pointer;
    sprites.colors[sprite] = static_cast<std::uint8_t>(
        assets::ZuulData(payload_).sprite_color(pointer) & 0x0FU);
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) {
        throw std::out_of_range("Zuul sprite pointer");
    }
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                sprites.bitmap_data[sprite].begin());
}

void Zuul::reset_sprite_state()
{
    building_registers_.animation77[0] = 0;
    auto& sprites = state_.city.sprites;
    sprites.x.fill(0);
    sprites.y.fill(0);
    sprites.target_x.fill(0);
    sprites.target_y.fill(0);
    for (std::size_t sprite = 0; sprite < 8; ++sprite) set_pointer(sprite, 0);
}

void Zuul::advance_state()
{
    state_.city.key17 = 0;
    state_.city.state3a = static_cast<std::uint8_t>(state_.city.state3a + 1U);
}

void Zuul::reset_gate()
{
    auto& sprites = state_.city.sprites;
    sprites.x[5] = 0xA8;
    sprites.y[5] = 0xC7;
    sprites.target_x[5] = 0x60;
    sprites.target_y[5] = 0xC0;
    registers_.gate_phase_ea77 = 1;
}

void Zuul::move_sprite(std::size_t sprite)
{
    // $9C25 always stores its A=2 movement limit, even when both coordinates
    // are already at their targets.
    registers_.scratch24 = 2;
    auto approach = [this](std::uint8_t& value, std::uint8_t target) {
        if (value == target) return;
        registers_.scratch23 = 1;
        if (value < target) ++value;
        else --value;
    };
    auto& sprites = state_.city.sprites;
    approach(sprites.x[sprite], sprites.target_x[sprite]);
    approach(sprites.y[sprite], sprites.target_y[sprite]);
}

void Zuul::move_control_target(std::size_t sprite, std::uint8_t joystick)
{
    auto& sprites = state_.city.sprites;
    registers_.scratch24 = joystick;
    if ((joystick & 0x08U) == 0) ++sprites.target_x[sprite];
    if ((joystick & 0x04U) == 0) --sprites.target_x[sprite];
    if ((joystick & 0x02U) == 0) ++sprites.target_y[sprite];
    if ((joystick & 0x01U) == 0) --sprites.target_y[sprite];
}

bool Zuul::sprite_at_target(std::size_t sprite) const noexcept
{
    const auto& sprites = state_.city.sprites;
    return sprites.x[sprite] == sprites.target_x[sprite] &&
           sprites.y[sprite] == sprites.target_y[sprite];
}

void Zuul::animate_buster5(std::uint8_t frame)
{
    auto& sprites = state_.city.sprites;
    auto& animation = building_registers_.animation77[0];
    if ((frame & 1U) == 0) {
        auto direction = static_cast<std::uint8_t>(animation & 1U);
        if (sprites.x[5] != sprites.target_x[5]) {
            direction = sprites.x[5] >= sprites.target_x[5] ? 1U : 0U;
        }
        registers_.scratch24 = direction;
        animation = static_cast<std::uint8_t>(animation + 2U);
        if (animation >= 8U) animation = 0;
        animation = static_cast<std::uint8_t>((animation & 0xFEU) | direction);
    }
    const bool settled = sprite_at_target(5);
    set_pointer(5, settled ? static_cast<std::uint8_t>((animation & 1U) | 0x0EU)
                           : static_cast<std::uint8_t>(animation + 0x0EU));
}

ZuulTickResult Zuul::request_speech(std::uint8_t command, ZuulInput input)
{
    blocked_speech_ = command;
    blocked_input_ = input;
    return {{{ZuulAudioAction::speech_start, command}}, true, false, false};
}

ZuulTickResult Zuul::update_controlled_sprite(ZuulInput input)
{
    auto& city = state_.city;
    auto& sprites = city.sprites;
    // Movement and input target only the controlled buster. Gate sprite
    // coordinates must survive collision speech until gate animation resumes.
    sprites.target_x[5] = std::clamp<std::uint8_t>(sprites.target_x[5], 0x20, 0x8C);

    bool collision_read = false;
    if ((input.irq_counter08 & 3U) == 0 && sprites.target_y[5] < 0xB9U) {
        collision_read = true;
        if ((input.sprite_sprite_collision_d01e & 0x2FU) != 0) {
            sprites.target_x[5] = 0x30;
            sprites.target_y[5] = 0xE5;
            set_pointer(5, 0x18);
            city.characters.screen[0x3FD] = 0x18;
            --registers_.keymaster_ea79;
            --city.backup_men3d;
            auto result = request_speech(3, input);
            result.collision_read = true;
            return result;
        }
    }

    if (sprites.target_y[5] < 0xAAU &&
        sprites.target_x[5] >= 0x5BU && sprites.target_x[5] < 0x5FU) {
        reset_gate();
        set_pointer(0, 0x2F);
        set_pointer(2, 0x30);
        --registers_.gatekeeper_ea78;
        --city.backup_men3d;
        city.countdown7c = 0x3F;
        return {{}, false, collision_read, false};
    }
    if (sprites.target_y[5] < 0xAAU) sprites.target_y[5] = 0xAA;
    if (sprites.target_y[5] >= 0xC8U) sprites.target_y[5] = 0xC8;
    return {{}, false, collision_read, false};
}

ZuulTickResult Zuul::animate_gate(std::uint8_t irq_counter)
{
    auto& sprites = state_.city.sprites;
    auto phase = static_cast<std::uint8_t>((irq_counter >> 1U) & 0x3FU);
    if (phase >= 0x20U) phase = static_cast<std::uint8_t>(phase ^ 0x3FU);
    phase = static_cast<std::uint8_t>(phase & 0x1FU);

    const auto gate = assets::ZuulData(payload_).gate_frame(phase);
    sprites.y[0] = sprites.y[2] = gate.y;
    sprites.y[1] = sprites.y[3] = static_cast<std::uint8_t>(sprites.y[0] + 0x2AU);
    sprites.x[0] = sprites.x[1] = gate.x;
    sprites.x[2] = sprites.x[3] = static_cast<std::uint8_t>(sprites.x[0] + 0x18U);
    const auto packed = gate.packed_pointers;
    set_pointer(3, static_cast<std::uint8_t>((packed & 0x0FU) + 0x2EU));
    set_pointer(1, static_cast<std::uint8_t>((packed >> 4U) + 0x2CU));
    return {{}, false, false, true};
}

ZuulTickResult Zuul::tick_state40()
{
    reset_sprite_state();
    state_.city.countdown7c = 0;
    for (std::size_t sprite = 0; sprite < 4; ++sprite) {
        set_pointer(sprite, assets::ZuulData(payload_).rooftop_pointer(sprite));
    }
    auto& sprites = state_.city.sprites;
    sprites.multicolor_mask = 0xF0;
    sprites.x_expand_mask = 0x0F;
    sprites.y_expand_mask = 0x0F;
    state_.city.backup_men3d = 3;
    registers_.gatekeeper_ea78 = 2;
    registers_.keymaster_ea79 = 2;
    reset_gate();
    advance_state();
    return {};
}

ZuulTickResult Zuul::tick_state41(ZuulInput input)
{
    auto& city = state_.city;
    if (city.countdown7c == 0) {
        // Passes and collisions have independent outcome counters. Check
        // passes first: remaining busters alone never selects the ending.
        if (registers_.gatekeeper_ea78 == 0) {
            city.countdown7c = 0xCF;
            reset_sprite_state();
            advance_state();
            return {};
        }
        if (registers_.keymaster_ea79 == 0) {
            city.state3a = 0x2E;
            return {};
        }
        const assets::ZuulData data(payload_);
        set_pointer(0, data.rooftop_pointer(0));
        set_pointer(2, data.rooftop_pointer(2));

        if ((registers_.gate_phase_ea77 & 0x80U) != 0) {
            move_sprite(5);
            if (!sprite_at_target(5)) return animate_gate(input.irq_counter08);
            reset_gate();
            city.countdown7c = 0x2F;
        }
        if (registers_.gate_phase_ea77 != 0) {
            registers_.scratch23 = 0x0E;
            animate_buster5(input.frame09);
            move_sprite(5);
            if (sprite_at_target(5)) registers_.gate_phase_ea77 = 0;
        } else {
            registers_.scratch23 = 0x0E;
            animate_buster5(input.frame09);
            move_sprite(5);
            move_control_target(5, input.joystick33);
            auto collision = update_controlled_sprite(input);
            if (collision.blocking_request) return collision;
            auto tail = animate_gate(input.irq_counter08);
            tail.collision_read = collision.collision_read;
            return tail;
        }
    }
    return animate_gate(input.irq_counter08);
}

ZuulTickResult Zuul::tick_state42()
{
    auto& city = state_.city;
    if (city.countdown7c == 0) {
        --city.countdown7c;
        advance_state();
        return {};
    }
    std::fill_n(city.characters.screen.begin() + 0x370, 40, 0);
    return {};
}

void Zuul::scroll_climb_background()
{
    auto& city = state_.city;
    auto& frame = city.characters;
    // The unrolled helper covers $5400-$571F only: rows 0-19 move down to
    // rows 1-20. Rows 21-24 are deliberately untouched.
    std::move_backward(frame.screen.begin(), frame.screen.begin() + 20 * 40,
                       frame.screen.begin() + 21 * 40);
    std::move_backward(frame.colors.begin(), frame.colors.begin() + 20 * 40,
                       frame.colors.begin() + 21 * 40);

    if (city.countdown7c >= 0x15U) {
        if ((city.countdown7c & 3U) == 3U) --city.countdown7c;
        const auto phase = static_cast<std::uint8_t>(city.countdown7c & 3U);
        const auto row = assets::ZuulData(payload_).generated_climb_row(phase);
        std::copy(row.colors.begin(), row.colors.end(), frame.colors.begin());
        std::copy(row.screen.begin(), row.screen.end(), frame.screen.begin());
        return;
    }

    const auto row = assets::ZuulData(payload_).static_climb_row(city.countdown7c);
    climb_color_offset_ = static_cast<std::size_t>(city.countdown7c) * 40U;
    std::copy(row.screen.begin(), row.screen.end(), frame.screen.begin());
    std::copy(row.colors.begin(), row.colors.end(), frame.colors.begin());
}

ZuulTickResult Zuul::tick_state43()
{
    auto& city = state_.city;
    auto& sprites = city.sprites;
    scroll_climb_background();
    std::uint8_t y = 0;
    if (city.countdown7c < 0x19U) {
        y = static_cast<std::uint8_t>((0x19U - city.countdown7c) * 8U - 0x1BU);
        if (city.countdown7c == 0) y = static_cast<std::uint8_t>(y - 8U);
    }
    sprites.y[5] = sprites.y[6] = y;
    sprites.x[5] = 0x5E;
    sprites.x[6] = 0x84;
    constexpr std::array<std::uint8_t, 8> pointers{0x19,0x19,0x19,0x19,0,0x16,0x17,0};
    for (std::size_t sprite = 0; sprite < pointers.size(); ++sprite) {
        set_pointer(sprite, pointers[sprite]);
    }
    building_registers_.animation77[0] = 0;
    sprites.x_expand_mask = 0;
    building_registers_.animation77[1] = 1;
    if (city.countdown7c == 0) {
        --city.countdown7c;
        advance_state();
    }
    return {};
}

void Zuul::update_climb_sprites()
{
    auto& city = state_.city;
    auto& sprites = city.sprites;
    sprites.y_expand_mask = 0x0F;
    registers_.beam_scratch7a = 0;
    for (std::size_t buster = 0; buster < 2; ++buster) {
        const auto source = 5U + buster;
        const auto upper = buster;
        const auto lower = buster + 2U;
        const auto facing = static_cast<std::uint8_t>(
            building_registers_.animation77[buster] & 1U);
        constexpr std::array<std::uint8_t, 2> upper_x{0x08, 0xF8};
        constexpr std::array<std::uint8_t, 2> lower_delta{0x09, 0xF7};
        sprites.x[upper] = sprites.target_x[upper] =
            static_cast<std::uint8_t>(sprites.x[source] + upper_x[facing]);
        sprites.x[lower] = sprites.target_x[lower] =
            static_cast<std::uint8_t>(sprites.x[upper] + lower_delta[facing]);
        sprites.y[upper] = sprites.target_y[upper] =
            static_cast<std::uint8_t>(sprites.y[source] - 0x29U);
        sprites.y[lower] = sprites.target_y[lower] =
            static_cast<std::uint8_t>(sprites.y[upper] - 0x28U);
    }
    registers_.beam_phase79 = static_cast<std::uint8_t>(registers_.beam_phase79 + 1U);
    if (registers_.beam_phase79 >= 6U) registers_.beam_phase79 = 0;
    constexpr std::array<std::uint8_t, 2> bases{0x19, 0x3C};
    for (std::size_t buster = 0; buster < 2; ++buster) {
        const auto pointer = static_cast<std::uint8_t>(
            bases[building_registers_.animation77[buster] & 1U] +
            registers_.beam_phase79);
        set_pointer(buster, pointer);
        set_pointer(buster + 2U, pointer);
    }
}

void Zuul::add_reward()
{
    auto& balance = state_.city.balance57;
    bool carry = false;
    balance.byte59 = bcd_add(balance.byte59, 0, carry);
    balance.byte58 = bcd_add(balance.byte58, 0x50, carry);
    balance.byte57 = bcd_add(balance.byte57, 0, carry);
    if (carry) balance.byte57 = balance.byte58 = 0x99;
}

ZuulTickResult Zuul::tick_state44()
{
    auto& city = state_.city;
    city.backpack_charge3e = 1;
    update_climb_sprites();
    const auto phase = static_cast<std::uint8_t>(city.countdown7c >> 4U);
    if (phase >= 9U) return {};
    registers_.scratch23 = phase;
    const auto row = static_cast<std::size_t>(8U - phase);
    std::fill_n(city.characters.screen.begin() + 0x156 + row * 40U, 8, 0xA8);
    if (city.countdown7c == 0x10U) return request_speech(4);
    if (city.countdown7c == 0) {
        city.state3a = 0x36;
        registers_.scratch23 = 0;
        registers_.scratch24 = 0x50;
        registers_.scratch25 = 0;
        add_reward();
    }
    return {};
}

ZuulTickResult Zuul::tick(ZuulInput input)
{
    climb_color_offset_.reset();
    if (blocked_speech_ != 0) return {{}, true, false, false};
    switch (state_.city.state3a) {
    case 0x28: return tick_state40();
    case 0x29: return tick_state41(input);
    case 0x2A: return tick_state42();
    case 0x2B: return tick_state43();
    case 0x2C: return tick_state44();
    default: return {};
    }
}

ZuulTickResult Zuul::resume_speech()
{
    if (blocked_speech_ == 0) return {};
    const auto command = std::exchange(blocked_speech_, static_cast<std::uint8_t>(0));
    if (command == 3) {
        state_.city.countdown7c = 0x2F;
        registers_.gate_phase_ea77 = 0xFF;
        return animate_gate(blocked_input_.irq_counter08);
    }
    return {};
}

} // namespace ghostbusters::game
