#pragma once

#include "assets/capture_data.hpp"
#include "game/drive_controls.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// Persistent bytes read or retained by States $18-$1A outside
// DriveControlsState. There are deliberately no field defaults.
struct BuildingControlsRegisters {
    BuildingControlsRegisters(std::uint8_t state1e_value,
                              std::uint8_t scratch37_value,
                              std::array<std::uint8_t, 2> animation77_values,
                              std::uint8_t direction7d_value,
                              std::array<std::uint8_t, 2> beam_direction7e_values)
        : state1e(state1e_value), scratch37(scratch37_value),
          animation77(animation77_values),
          direction7d(direction7d_value), beam_direction7e(beam_direction7e_values)
    {
    }

    std::uint8_t state1e;
    std::uint8_t scratch37;
    std::array<std::uint8_t, 2> animation77;
    std::uint8_t direction7d;
    std::array<std::uint8_t, 2> beam_direction7e;
};

struct BuildingControlsInput {
    std::uint8_t random06 = 0;
    std::uint8_t frame09 = 0;
    std::uint8_t joystick33 = 0xFF;
};

// Native boundary for State $18 at $84F2-$852A, State $19 at
// $852B-$8561 and State $1A at $8562-$859A.
class BuildingControls {
public:
    BuildingControls(const assets::Payload& payload, DriveControlsState state,
                     BuildingControlsRegisters registers);

    // Executes exactly one handler. Other state values are stable handoff states.
    void tick(BuildingControlsInput input = {});

    [[nodiscard]] DriveControlsState& state() noexcept { return state_; }
    [[nodiscard]] const DriveControlsState& state() const noexcept { return state_; }
    [[nodiscard]] BuildingControlsRegisters& registers() noexcept { return registers_; }
    [[nodiscard]] const BuildingControlsRegisters& registers() const noexcept
    {
        return registers_;
    }

private:
    void move_all_sprites();
    void move_sprite(std::size_t sprite);
    void move_control_target(std::size_t sprite, std::uint8_t joystick);
    void clamp_control_target(std::size_t sprite);
    void animate_pair(std::size_t sprite, std::size_t animation,
                      std::uint8_t settled_pointer, std::uint8_t frame);
    void animate_both(std::uint8_t settled_pointer, std::uint8_t frame);
    void position_outer_pair();
    void update_ghost(BuildingControlsInput input);
    [[nodiscard]] bool check_timeout_and_cleanup();
    [[nodiscard]] bool fire_press(std::uint8_t joystick);
    void set_pointer(std::size_t sprite, std::uint8_t pointer);
    void prepare_beam_directions();

    assets::CaptureData capture_data_;
    DriveControlsState state_;
    BuildingControlsRegisters registers_;
    std::vector<std::uint8_t> scene_data_;
};

} // namespace ghostbusters::game
