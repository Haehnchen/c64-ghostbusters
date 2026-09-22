#include "game/drive_controls.hpp"
#include "assets/vehicle_data.hpp"
#include "assets/capture_data.hpp"
#include "assets/city_data.hpp"

#include "game/title_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

bool in_half_open(std::uint8_t value, std::uint8_t low, std::uint8_t high)
{
    return value >= low && value < high;
}

} // namespace

DriveControls::DriveControls(const assets::Payload& payload, const DriveEntry& entry,
                             DriveControlsPersistent persistent)
    : payload_(payload)
{
    if (entry.stage() != DriveEntry::Stage::ready) {
        throw std::invalid_argument("DriveControls requires a ready DriveEntry");
    }
    state_.city = entry.state();
    state_.vehicle5c = entry.vehicle();
    state_.distance67 = entry.distance67();
    state_.vehicle_position63 = entry.vehicle_position63();
    state_.scroll_position1a = entry.state1a();
    state_.speed1b = entry.state1b();
    state_.scroll_fraction1c = entry.state1c();
    state_.direction64 = persistent.direction64;
    state_.animation65 = persistent.animation65;
    state_.fire_latch13 = persistent.fire_latch13;
    state_.roamer_source73 = persistent.roamer_source73;
    state_.capture_timer75 = persistent.capture_timer75;

    scene_data_ = shared_sprite_data(payload_);
    for (std::size_t sprite = 0; sprite < 8; ++sprite) refresh_sprite_visual(sprite);
}

DriveControls::DriveControls(const assets::Payload& payload, DriveControlsState state)
    : payload_(payload), state_(std::move(state))
{
    if (state_.vehicle5c >= 4) throw std::out_of_range("Vehicle index must be in 0..3");
    scene_data_ = shared_sprite_data(payload_);
    for (std::size_t sprite = 0; sprite < 8; ++sprite) refresh_sprite_visual(sprite);
}

void DriveControls::refresh_sprite_visual(std::size_t sprite)
{
    const auto pointer = state_.city.sprites.pointers[sprite];
    const auto offset = static_cast<std::size_t>(pointer) * 64U;
    if (offset + 64U > scene_data_.size()) throw std::out_of_range("drive sprite pointer");
    std::copy_n(scene_data_.begin() + static_cast<std::ptrdiff_t>(offset), 64,
                state_.city.sprites.bitmap_data[sprite].begin());
    state_.city.sprites.colors[sprite] =
        assets::CaptureData(payload_).sprite_color(pointer);
}

void DriveControls::move_sprite(std::size_t sprite)
{
    auto approach = [](std::uint8_t& value, std::uint8_t target) {
        const auto delta = static_cast<std::uint8_t>(target - value);
        if (delta == 0) return;
        const auto amount = static_cast<std::uint8_t>(delta < 3 ? delta : 2);
        value = target >= value ? static_cast<std::uint8_t>(value + amount)
                                : static_cast<std::uint8_t>(value - amount);
    };
    approach(state_.city.sprites.x[sprite], state_.city.sprites.target_x[sprite]);
    approach(state_.city.sprites.y[sprite], state_.city.sprites.target_y[sprite]);
}

void DriveControls::reset_roamer_shadow(std::uint8_t source)
{
    state_.city.active_roamer_count28 = static_cast<std::uint8_t>(std::count_if(
        state_.city.sprite_control_c0.begin() + 4,
        state_.city.sprite_control_c0.end(),
        [](std::uint8_t value) { return value != 0; }));
    const auto sprite = static_cast<std::size_t>(source + 4);
    const auto initial = assets::CityData(payload_).arriving_sprite(sprite);
    state_.city.shadow_x_ea46[sprite] = initial.x;
    state_.city.shadow_y_ea47[sprite] = initial.y;
    state_.city.shadow_target_x_ea56[sprite] = initial.target_x;
    state_.city.shadow_target_y_ea57[sprite] = initial.target_y;
}

std::uint8_t& DriveControls::fire_latch(std::size_t slot) noexcept
{
    return slot == 0 ? state_.city.fire_latch11 : state_.fire_latch13;
}

DriveControlsTickResult DriveControls::tick(DriveControlsInput input)
{
    DriveControlsTickResult result;
    if (state_.city.state3a != 0x15) return result;

    // $8073-$8077 overwrites the VIC-II sprite multicolor register on every
    // State 21 pass. X expansion is $D01D and remains independently animated.
    state_.city.sprites.multicolor_mask = 0xF8;

    auto fraction = static_cast<std::uint8_t>(state_.scroll_fraction1c + state_.speed1b);
    state_.scroll_fraction1c = fraction;
    auto position = static_cast<std::uint8_t>((fraction >> 4U) + state_.scroll_position1a);
    if (position >= 0x7F) {
        position = static_cast<std::uint8_t>(position - 0x7F);
        if (state_.city.route_length66 != state_.distance67 &&
            state_.city.route_length66 != 0xFF) {
            ++state_.city.route_length66;
        }
    }
    state_.scroll_position1a = position;

    for (int sprite = 1; sprite >= 0; --sprite) {
        if (state_.city.sprites.y[sprite] == 0) continue;
        move_sprite(static_cast<std::size_t>(sprite));
        if (state_.city.sprites.y[sprite] >= 0xE0) state_.city.sprites.y[sprite] = 0;
    }
    state_.scroll_fraction1c = static_cast<std::uint8_t>(state_.scroll_fraction1c & 0x0F);

    if (state_.city.route_length66 != state_.distance67) {
        ++state_.speed1b;
        const auto limit = assets::VehicleData(payload_).speed_limit(state_.vehicle5c);
        if (state_.speed1b >= limit) state_.speed1b = limit;
    } else if (state_.speed1b != 0) {
        --state_.speed1b;
    }

    state_.city.sprites.y[5] = state_.city.sprites.y[7] = state_.scroll_position1a;
    const auto opposite = static_cast<std::uint8_t>(state_.scroll_position1a + 0x7F);
    state_.city.sprites.y[4] = state_.city.sprites.y[6] = opposite;

    if (state_.city.route_length66 != state_.distance67) {
        if ((input.joystick33 & 4) == 0 && state_.vehicle_position63 >= 9) {
            state_.direction64 = 0xFF;
        }
        if ((input.joystick33 & 8) == 0 && state_.vehicle_position63 < 0x64) {
            state_.direction64 = 1;
        }
    } else if (state_.vehicle_position63 < 0x64) {
        state_.direction64 = 1;
    }

    ++state_.animation65;
    state_.animation65 = static_cast<std::uint8_t>(state_.animation65 | 0x80);

    auto random = input.random06;
    for (int source = 3; source >= 0; --source) {
        auto& counter = state_.city.roamer_counters6f[source];
        if (counter == 0 || counter != state_.city.route_length66) continue;
        int slot = -1;
        if (state_.city.sprites.y[0] == 0) slot = 0;
        else if (state_.city.sprites.y[1] == 0) slot = 1;
        if (slot < 0) {
            ++counter;
            continue;
        }
        counter = 0;
        state_.roamer_source73[slot] = static_cast<std::uint8_t>(source);
        ++state_.city.sprites.y[slot];

        random = static_cast<std::uint8_t>((random << 1U) | (random >= 0x80 ? 1U : 0U));
        const auto x = static_cast<std::uint8_t>((random & 0x7F) + 0x10);
        state_.city.sprites.x[slot] = state_.city.sprites.target_x[slot] = x;
        state_.city.sprites.target_y[slot] = 0xF0;
        const auto pointer = static_cast<std::uint8_t>((random & 1) + 8);
        state_.city.sprites.pointers[slot] = pointer;
        refresh_sprite_visual(static_cast<std::size_t>(slot));
        const auto mask = static_cast<std::uint8_t>(1U << slot);
        state_.city.sprites.x_expand_mask =
            static_cast<std::uint8_t>(state_.city.sprites.x_expand_mask | mask);
        state_.city.sprites.y_expand_mask =
            static_cast<std::uint8_t>(state_.city.sprites.y_expand_mask | mask);
    }

    if (state_.city.route_length66 == state_.distance67 && state_.speed1b == 0) {
        state_.city.key17 = 0;
        ++state_.city.state3a;
        return result;
    }

    const auto vehicle_x = state_.city.sprites.x[2];
    const auto target_x = static_cast<std::uint8_t>(vehicle_x - 4);
    const auto target_y = assets::VehicleData(payload_).vacuum_target_y(state_.vehicle5c);
    for (int slot = 1; slot >= 0; --slot) {
        if (state_.city.sprites.y[slot] == 0) continue;
        if (state_.city.sprites.target_y[slot] == 0xF0) {
            const auto current_fire = static_cast<std::uint8_t>(input.joystick33 & 0x10);
            auto& latch = fire_latch(static_cast<std::size_t>(slot));
            const bool pressed = current_fire != latch && current_fire == 0;
            latch = current_fire;
            if (!pressed ||
                !in_half_open(state_.city.sprites.x[slot],
                              static_cast<std::uint8_t>(vehicle_x - 0x20),
                              static_cast<std::uint8_t>(vehicle_x + 0x10)) ||
                (state_.city.owned_mask6d & 0x20) == 0) {
                continue;
            }
        }
        state_.city.sprites.target_x[slot] = target_x;
        state_.city.sprites.target_y[slot] = target_y;
    }

    for (int slot = 1; slot >= 0; --slot) {
        const auto timer = state_.capture_timer75[slot];
        if (timer == 0) continue;
        const auto x = timer >= 0x11 ? target_x : static_cast<std::uint8_t>(target_x + 7);
        state_.city.sprites.x[slot] = state_.city.sprites.target_x[slot] = x;
        state_.city.sprites.y[slot] = state_.city.sprites.target_y[slot] = target_y;
    }

    bool effect_busy = input.voice3_effect_busy;
    for (int slot = 1; slot >= 0; --slot) {
        const auto x_delta = static_cast<std::uint8_t>(
            state_.city.sprites.x[slot] ^ state_.city.sprites.target_x[slot]);
        if ((x_delta & 0xFE) != 0 ||
            state_.city.sprites.y[slot] != state_.city.sprites.target_y[slot] ||
            state_.capture_timer75[slot] != 0) {
            continue;
        }
        state_.capture_timer75[slot] = 0x20;
        ++result.effect0_calls;
        if (!effect_busy) {
            result.started_effects.push_back(0);
            effect_busy = true;
        }
    }

    for (int slot = 1; slot >= 0; --slot) {
        auto& timer = state_.capture_timer75[slot];
        if (timer == 0) continue;
        const auto old = timer;
        --timer;
        std::uint8_t pointer_source = timer;
        if (timer == 0) {
            state_.city.sprites.y[slot] = 0;
            reset_roamer_shadow(state_.roamer_source73[slot]);
            pointer_source = state_.city.shadow_target_y_ea57[state_.roamer_source73[slot] + 4];
        }
        state_.city.sprites.pointers[slot] =
            static_cast<std::uint8_t>(((pointer_source & 4) >> 2U) + 8);
        refresh_sprite_visual(static_cast<std::size_t>(slot));
        const auto clear_mask = static_cast<std::uint8_t>(~(1U << slot));
        if (old == 0x10) {
            state_.city.sprites.x_expand_mask =
                static_cast<std::uint8_t>(state_.city.sprites.x_expand_mask & clear_mask);
        }
        if (old == 0x08) {
            state_.city.sprites.y_expand_mask =
                static_cast<std::uint8_t>(state_.city.sprites.y_expand_mask & clear_mask);
        }
    }
    return result;
}

} // namespace ghostbusters::game
