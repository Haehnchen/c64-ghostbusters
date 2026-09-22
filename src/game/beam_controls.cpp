#include "game/beam_controls.hpp"

#include "assets/sprite_mirror.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

[[nodiscard]] constexpr std::uint8_t absolute_difference(std::uint8_t a,
                                                          std::uint8_t b)
{
    return a >= b ? static_cast<std::uint8_t>(a - b)
                  : static_cast<std::uint8_t>(b - a);
}

} // namespace

BeamControls::BeamControls(const assets::Payload& payload, DriveControlsState state,
                           BuildingControlsRegisters building_registers,
                           BeamControlsRegisters registers)
    : capture_data_(payload), notice_data_(payload), state_(std::move(state)),
      building_registers_(building_registers), registers_(registers)
{
    if (state_.city.state3a != 0x1B) {
        throw std::invalid_argument("BeamControls requires State $1B");
    }

    const auto decoded = capture_data_.scene_graphics();

    scene_data_.assign(0x1100, 0);
    std::copy(decoded.begin(), decoded.end(), scene_data_.begin());
    assets::mirror_sprite_sheets(scene_data_);
}

void BeamControls::set_pointer(std::size_t sprite, std::uint8_t pointer)
{
    auto& sprites = state_.city.sprites;
    sprites.pointers[sprite] = pointer;
    // The raster IRQ resolves every pointer through $A949 before writing the
    // VIC color register. Cache the register-visible low nibble together with
    // the bitmap selected by that pointer.
    sprites.colors[sprite] = static_cast<std::uint8_t>(
        capture_data_.sprite_color(pointer) & 0x0FU);
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) {
        throw std::out_of_range("beam-controls sprite pointer");
    }
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                sprites.bitmap_data[sprite].begin());
}

void BeamControls::compute_beam_origins()
{
    const auto& sprites = state_.city.sprites;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto sprite = 5U + i;
        auto delta = static_cast<std::uint8_t>(sprites.y[sprite] - sprites.y[4]);
        delta = static_cast<std::uint8_t>(delta >> 2U);
        // $98FC-$9908 uses the low bit left in carry after shifting
        // (pointer-$0E), exactly the pointer's direction bit.
        if ((static_cast<std::uint8_t>(sprites.pointers[sprite] - 0x0EU) & 1U) != 0) {
            delta = static_cast<std::uint8_t>(0U - delta);
        }
        beam_origins_[i] = static_cast<std::uint8_t>(sprites.x[sprite] + delta);
    }
}

void BeamControls::compute_beam_directions()
{
    const auto ghost_x = state_.city.sprites.x[4];
    for (std::size_t i = 0; i < beam_origins_.size(); ++i) {
        building_registers_.beam_direction7e[i] =
            ghost_x >= beam_origins_[i] ? 0x00 : 0xFF;
    }
}

void BeamControls::update_ghost(BeamControlsInput input)
{
    auto& city = state_.city;
    auto& sprites = city.sprites;
    sprites.priority_mask = static_cast<std::uint8_t>(sprites.priority_mask & 0xEFU);
    if ((city.owned_mask6d & 0x02U) == 0) {
        sprites.priority_mask = static_cast<std::uint8_t>(sprites.priority_mask | 0x10U);
    }

    const auto direction = building_registers_.direction7d;
    const auto delta = capture_data_.ghost_delta(direction);
    sprites.x[4] = static_cast<std::uint8_t>(
        sprites.x[4] + delta[0]);
    sprites.y[4] = static_cast<std::uint8_t>(
        sprites.y[4] + delta[1]);
    std::uint8_t turn = 0;
    if (sprites.y[4] >= 0x70) { sprites.y[4] = 0x6E; turn = 4; }
    if (sprites.y[4] < 0x30) { sprites.y[4] = 0x32; turn = 4; }
    sprites.target_y[4] = sprites.y[4];
    if (sprites.x[4] >= 0x90) { sprites.x[4] = 0x8E; turn = 4; }
    if (sprites.x[4] < 0x48) { sprites.x[4] = 0x4A; turn = 4; }
    sprites.target_x[4] = sprites.x[4];
    building_registers_.direction7d =
        static_cast<std::uint8_t>((building_registers_.direction7d + turn) & 7U);

    compute_beam_origins();
    if ((input.frame09 & 3U) == 0) {
        const auto bit = static_cast<std::uint8_t>(input.random06 & 1U);
        building_registers_.direction7d =
            static_cast<std::uint8_t>((building_registers_.direction7d + bit) & 7U);
        set_pointer(4, static_cast<std::uint8_t>(0x0AU + bit));
    }
}

void BeamControls::confine_ghost()
{
    auto& sprites = state_.city.sprites;
    const auto origins = beam_origins_;
    const std::array<std::uint8_t, 2> buster_y = {sprites.y[5], sprites.y[6]};
    auto direction = building_registers_.direction7d;
    if (absolute_difference(origins[1], origins[0]) < 6U) direction = 6;

    for (int i = 1; i >= 0; --i) {
        // The original comparison is deliberately unsigned and wrapping.
        if (static_cast<std::uint8_t>(buster_y[i] - sprites.y[4]) >= 0x60U) {
            compute_beam_directions();
            return;
        }
        if (building_registers_.beam_direction7e[i] == 0) {
            if (origins[i] >= sprites.x[4]) {
                sprites.x[4] = origins[i];
                direction = 0;
            }
        } else if (origins[i] < sprites.x[4]) {
            sprites.x[4] = origins[i];
            direction = 4;
        }
    }
    building_registers_.direction7d = direction;
}

void BeamControls::move_all_sprites()
{
    auto approach = [](std::uint8_t& value, std::uint8_t target) {
        if (value < target) ++value;
        else if (value > target) --value;
    };
    auto& sprites = state_.city.sprites;
    for (std::size_t sprite = 8; sprite-- > 0;) {
        approach(sprites.x[sprite], sprites.target_x[sprite]);
        approach(sprites.y[sprite], sprites.target_y[sprite]);
    }
}

void BeamControls::move_control_target(std::size_t sprite, std::uint8_t joystick)
{
    auto& sprites = state_.city.sprites;
    if ((joystick & 0x08U) == 0) ++sprites.target_x[sprite];
    if ((joystick & 0x04U) == 0) --sprites.target_x[sprite];
    if ((joystick & 0x02U) == 0) ++sprites.target_y[sprite];
    if ((joystick & 0x01U) == 0) --sprites.target_y[sprite];
}

void BeamControls::clamp_control_target(std::size_t sprite)
{
    auto& sprites = state_.city.sprites;
    auto& x = sprites.target_x[sprite];
    auto& y = sprites.target_y[sprite];
    if (x < 0x20) x = 0x20;
    if (x >= 0x8C) x = 0x8C;
    if (y < 0xAA) y = 0xAA;
    if (y >= 0xC8) y = 0xC8;
}

void BeamControls::update_beam_sprites()
{
    auto& sprites = state_.city.sprites;
    sprites.y_expand_mask = 0x0F;
    registers_.beam_scratch7a = 0;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto buster = 5U + i;
        const bool left = (building_registers_.animation77[i] & 1U) != 0;
        const auto upper_x = static_cast<std::uint8_t>(sprites.x[buster] +
                                                       (left ? -8 : 8));
        const auto lower_x = static_cast<std::uint8_t>(upper_x +
                                                       (left ? -9 : 9));
        sprites.x[i] = sprites.target_x[i] = upper_x;
        sprites.x[i + 2] = sprites.target_x[i + 2] = lower_x;
        sprites.y[i] = sprites.target_y[i] =
            static_cast<std::uint8_t>(sprites.y[buster] - 0x29U);
        sprites.y[i + 2] = sprites.target_y[i + 2] =
            static_cast<std::uint8_t>(sprites.y[buster] - 0x51U);
    }

    ++registers_.beam_phase79;
    if (registers_.beam_phase79 >= 6U) registers_.beam_phase79 = 0;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto pointer = state_.city.backpack_charge3e == 0 ? 0U
            : static_cast<unsigned>(((building_registers_.animation77[i] & 1U) != 0
                                         ? 0x3CU : 0x19U) +
                                    registers_.beam_phase79);
        set_pointer(i, static_cast<std::uint8_t>(pointer));
        set_pointer(i + 2, static_cast<std::uint8_t>(pointer));
    }
}

bool BeamControls::beams_crossed(bool reversed) const
{
    const auto& sprites = state_.city.sprites;
    if (!reversed) {
        if (sprites.pointers[5] != 0x16 || sprites.pointers[6] != 0x17 ||
            sprites.x[6] < sprites.x[5]) return false;
    } else {
        if (sprites.pointers[6] != 0x16 || sprites.pointers[5] != 0x17 ||
            sprites.x[5] < sprites.x[6]) return false;
    }
    const auto current_distance = !reversed
        ? static_cast<std::uint8_t>(sprites.x[6] - sprites.x[5])
        : static_cast<std::uint8_t>(sprites.x[5] - sprites.x[6]);
    const auto limit = static_cast<std::uint8_t>(
        0x2AU - (absolute_difference(sprites.y[5], sprites.y[6]) >> 2U));
    return current_distance < limit;
}

bool BeamControls::fire_press(std::uint8_t joystick)
{
    const auto fire = static_cast<std::uint8_t>(joystick & 0x10U);
    const bool changed = fire != state_.city.fire_latch11;
    state_.city.fire_latch11 = fire;
    return changed && fire == 0;
}

void BeamControls::queue_notice(std::uint8_t index)
{
    std::vector<std::uint8_t> translated;
    for (auto value : notice_data_.notice(index)) {
        if (value == 0x20) value = 0;
        if (value >= 0x40) value = static_cast<std::uint8_t>(value - 0x40U);
        translated.push_back(value);
    }
    (void)state_.city.notices.queue(translated);
}

void BeamControls::finish_failure(BeamControlsTickResult& result)
{
    auto& city = state_.city;
    city.backpack_charge3e = 0;
    for (std::size_t sprite = 0; sprite < 4; ++sprite) set_pointer(sprite, 0);
    result.audio_events.push_back({BeamAudioAction::voice3_stop, 0});
    queue_notice(8);
    set_pointer(5, 0x18);
    set_pointer(6, 0x18);
    city.backup_men3d = static_cast<std::uint8_t>(city.backup_men3d - 2U);
    city.sprites.target_x[4] = 0;
    city.sprites.target_y[4] = 0;
    city.state3a = 0x1D;
    registers_.post_failure_ea7a = 0;
}

void BeamControls::finish_normally(BeamControlsTickResult& result)
{
    auto& sprites = state_.city.sprites;
    set_pointer(5, static_cast<std::uint8_t>((sprites.pointers[5] & 1U) | 0x0EU));
    set_pointer(6, static_cast<std::uint8_t>((sprites.pointers[6] & 1U) | 0x0EU));
    result.audio_events.push_back({BeamAudioAction::voice3_stop, 0});
    result.audio_events.push_back({BeamAudioAction::voice3_start, 3});
    // $8D86 clears the translated keyboard byte before incrementing $3A.
    state_.city.key17 = 0;
    ++state_.city.state3a;
}

BeamControlsTickResult BeamControls::tick(BeamControlsInput input)
{
    BeamControlsTickResult result;
    if (pending_irqs_ != 0) {
        result.wait_irqs = pending_irqs_;
        return result;
    }
    if (state_.city.state3a != 0x1B) return result;

    update_ghost(input);
    if (state_.city.backpack_charge3e != 0) confine_ghost();
    move_all_sprites();
    update_beam_sprites();

    auto& sprites = state_.city.sprites;
    const bool reversed = sprites.target_x[6] < sprites.target_x[5];
    if (absolute_difference(sprites.target_x[6], sprites.target_x[5]) >= 0x0CU) {
        const std::array<std::uint8_t, 2> masks = reversed
            ? std::array<std::uint8_t, 2>{0x0B, 0x07}
            : std::array<std::uint8_t, 2>{0x07, 0x0B};
        // $85BD starts at offset 2 (sprite 6) and then offset 0 (sprite 5).
        for (int i = 1; i >= 0; --i) {
            move_control_target(static_cast<std::size_t>(5 + i),
                                static_cast<std::uint8_t>(input.joystick33 | masks[i]));
            clamp_control_target(static_cast<std::size_t>(5 + i));
        }
    }

    // Y from $85B8 survives both target updates and clamps into the later
    // pointer/current-position crossing test.
    if (beams_crossed(reversed)) {
        pending_irqs_ = 0x40;
        result.wait_irqs = pending_irqs_;
        return result;
    }

    if (fire_press(input.joystick33)) {
        finish_normally(result);
        return result;
    }

    if ((input.frame09 & 0x0FU) != 0) return result;
    if (state_.city.backpack_charge3e != 0) {
        const auto charge = state_.city.backpack_charge3e;
        const auto low = static_cast<std::uint8_t>(charge & 0x0FU);
        state_.city.backpack_charge3e = low != 0
            ? static_cast<std::uint8_t>(charge - 1U)
            : static_cast<std::uint8_t>(charge - 7U);
    }
    if (state_.city.backpack_charge3e == 0) {
        queue_notice(3);
        finish_normally(result);
    }
    return result;
}

BeamControlsTickResult BeamControls::resume_irq()
{
    BeamControlsTickResult result;
    if (pending_irqs_ == 0) return result;
    update_beam_sprites();
    --pending_irqs_;
    result.wait_irqs = pending_irqs_;
    if (pending_irqs_ == 0) finish_failure(result);
    return result;
}

} // namespace ghostbusters::game
