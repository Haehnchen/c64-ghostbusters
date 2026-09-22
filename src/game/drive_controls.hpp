#pragma once

#include "assets/payload.hpp"
#include "game/city_controls.hpp"
#include "game/drive_entry.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// Bytes retained outside States 19/20.  They must be handed off explicitly;
// DriveEntry does not reset them in the original program.
struct DriveControlsPersistent {
    DriveControlsPersistent(std::uint8_t direction, std::uint8_t animation,
                            std::uint8_t second_fire_latch,
                            std::array<std::uint8_t, 2> roamer_sources,
                            std::array<std::uint8_t, 2> capture_timers)
        : DriveControlsPersistent(direction, animation, second_fire_latch,
                                  roamer_sources, capture_timers, 0, 0, 0, 0, 0)
    {
    }

    DriveControlsPersistent(std::uint8_t direction, std::uint8_t animation,
                            std::uint8_t second_fire_latch,
                            std::array<std::uint8_t, 2> roamer_sources,
                            std::array<std::uint8_t, 2> capture_timers,
                            std::uint8_t retained_scroll_position1a,
                            std::uint8_t retained_speed1b,
                            std::uint8_t retained_scroll_fraction1c,
                            std::uint8_t retained_vehicle_position63,
                            std::uint8_t retained_distance67)
        : direction64(direction), animation65(animation), fire_latch13(second_fire_latch),
          roamer_source73(roamer_sources), capture_timer75(capture_timers),
          retained1a(retained_scroll_position1a), retained1b(retained_speed1b),
          retained1c(retained_scroll_fraction1c), retained63(retained_vehicle_position63),
          retained67(retained_distance67)
    {
    }

    std::uint8_t direction64;
    std::uint8_t animation65;
    std::uint8_t fire_latch13;
    std::array<std::uint8_t, 2> roamer_source73;
    std::array<std::uint8_t, 2> capture_timer75;
    std::uint8_t retained1a;
    std::uint8_t retained1b;
    std::uint8_t retained1c;
    std::uint8_t retained63;
    std::uint8_t retained67;
};

struct DriveControlsState {
    CityControlsState city{};
    std::uint8_t vehicle5c = 0;
    std::uint8_t distance67 = 0;
    std::uint8_t vehicle_position63 = 0;
    std::uint8_t direction64 = 0;
    std::uint8_t animation65 = 0;
    std::uint8_t scroll_position1a = 0;
    std::uint8_t speed1b = 0;
    std::uint8_t scroll_fraction1c = 0;
    std::uint8_t fire_latch13 = 0;
    std::array<std::uint8_t, 2> roamer_source73{};
    std::array<std::uint8_t, 2> capture_timer75{};
};

struct DriveControlsInput {
    std::uint8_t random06 = 0;
    std::uint8_t joystick33 = 0xFF;
    // Mirrors the $E8 guard in start_voice3_effect. The shared EffectPlayer is
    // clocked outside this state handler.
    bool voice3_effect_busy = false;
};

struct DriveControlsTickResult {
    // Number of JSR $3D24,Y=0 calls made by the handler. Only accepted starts
    // appear in started_effects because $3D24 ignores calls while $E8 != 0.
    std::uint8_t effect0_calls = 0;
    std::vector<std::uint8_t> started_effects{};
};

enum class DriveControlsTransition : std::uint8_t {
    driving = 0x15,
    building = 0x16,
};

// Complete native boundary for State 21, $8073-$8278.
class DriveControls {
public:
    DriveControls(const assets::Payload& payload, const DriveEntry& entry,
                  DriveControlsPersistent persistent);
    DriveControls(const assets::Payload& payload, DriveControlsState state);

    [[nodiscard]] DriveControlsTickResult tick(DriveControlsInput input);

    [[nodiscard]] DriveControlsState& state() noexcept { return state_; }
    [[nodiscard]] const DriveControlsState& state() const noexcept { return state_; }
    [[nodiscard]] DriveControlsTransition transition() const noexcept
    {
        return static_cast<DriveControlsTransition>(state_.city.state3a);
    }

private:
    void refresh_sprite_visual(std::size_t sprite);
    void move_sprite(std::size_t sprite);
    void reset_roamer_shadow(std::uint8_t source);
    [[nodiscard]] std::uint8_t& fire_latch(std::size_t slot) noexcept;

    const assets::Payload& payload_;
    DriveControlsState state_;
    std::vector<std::uint8_t> scene_data_;
};

} // namespace ghostbusters::game
