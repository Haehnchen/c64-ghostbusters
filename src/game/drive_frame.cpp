#include "game/drive_frame.hpp"
#include "assets/vehicle_data.hpp"

#include <cstddef>

namespace ghostbusters::game {
namespace {

void scroll_grid_left(DriveControlsState& state)
{
    --state.vehicle_position63;
    if ((state.vehicle_position63 & 3) != 3) return;

    std::size_t x = state.vehicle_position63 >> 2U;
    for (std::size_t column = 0; column < 13; ++column, ++x) {
        for (std::size_t row = 0; row < 16; ++row) {
            const auto source = 0xA0U + row * 0x28U + x;
            state.city.characters.screen[source - 1] = state.city.characters.screen[source];
        }
    }
}

void scroll_grid_right(DriveControlsState& state)
{
    ++state.vehicle_position63;
    if ((state.vehicle_position63 & 3) != 0) return;

    std::size_t x = (state.vehicle_position63 >> 2U) + 12U;
    for (std::size_t column = 0; column < 13; ++column, --x) {
        for (std::size_t row = 0; row < 16; ++row) {
            const auto source = 0x9FU + row * 0x28U + x;
            state.city.characters.screen[source + 1] = state.city.characters.screen[source];
        }
    }
}

} // namespace

void update_drive_frame(const assets::Payload& payload, DriveControlsState& state,
                        std::uint8_t& vic_control2_d016)
{
    const assets::VehicleData data(payload);
    if (state.direction64 != 0) {
        if ((state.direction64 & 0x80) != 0) {
            scroll_grid_left(state);
            ++state.direction64;
        } else {
            scroll_grid_right(state);
            --state.direction64;
        }
    }

    if (state.city.state3a == 0x15) {
        const auto fine_scroll = static_cast<std::uint8_t>((state.vehicle_position63 & 3) << 1U);
        vic_control2_d016 = static_cast<std::uint8_t>((vic_control2_d016 & 0xF8) | fine_scroll);
        const auto vehicle_x = static_cast<std::uint8_t>(state.vehicle_position63 + 0x1E);
        state.city.sprites.x[2] = state.city.sprites.x[3] = vehicle_x;
        const auto vehicle_y = data.drive_sprite_y(state.vehicle5c);
        state.city.sprites.y[2] = state.city.sprites.y[3] = vehicle_y;
    }

    if (state.animation65 != 0) {
        auto phase = static_cast<std::uint8_t>(state.animation65 & 0x1F);
        if (phase >= 0x10) phase ^= 0x1F;
        phase >>= 2U;
        state.city.sprites.colors[2] = data.siren_color(phase);
    }
}

} // namespace ghostbusters::game
