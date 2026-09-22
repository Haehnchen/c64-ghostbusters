#pragma once

#include "assets/payload.hpp"
#include "game/building_controls.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// Persistent bytes used by States $22/$23 outside DriveControlsState and
// BuildingControlsRegisters. There are deliberately no field defaults.
struct HeadquartersRegisters {
    HeadquartersRegisters(std::uint8_t total_traps,
                           std::uint8_t full_traps,
                           std::uint8_t scratch23_value)
        : total_traps6a(total_traps), full_traps6c(full_traps),
          scratch23(scratch23_value)
    {
    }

    std::uint8_t total_traps6a;
    std::uint8_t full_traps6c;
    // Temporary helper byte $23 is cleared by State $23 before animation and
    // may become one while $9AE3 moves a coordinate.
    std::uint8_t scratch23;
};

struct HeadquartersInput {
    std::uint8_t frame09 = 0;
};

enum class HeadquartersTransition : std::uint8_t {
    city = 0x11,
    initialize = 0x22,
    returning = 0x23,
};

// Native boundary for State $22 at $88B6-$88DB and State $23 at
// $88DC-$8924. Each tick executes one complete original handler.
class Headquarters {
public:
    Headquarters(const assets::Payload& payload, DriveControlsState state,
                 BuildingControlsRegisters building_registers,
                 HeadquartersRegisters registers);

    void tick(HeadquartersInput input = {});

    [[nodiscard]] DriveControlsState& state() noexcept { return state_; }
    [[nodiscard]] const DriveControlsState& state() const noexcept { return state_; }
    [[nodiscard]] BuildingControlsRegisters& building_registers() noexcept
    {
        return building_registers_;
    }
    [[nodiscard]] const BuildingControlsRegisters& building_registers() const noexcept
    {
        return building_registers_;
    }
    [[nodiscard]] HeadquartersRegisters& registers() noexcept { return registers_; }
    [[nodiscard]] const HeadquartersRegisters& registers() const noexcept
    {
        return registers_;
    }
    [[nodiscard]] HeadquartersTransition transition() const noexcept
    {
        return static_cast<HeadquartersTransition>(state_.city.state3a);
    }

private:
    void reset_sprite_state();
    void set_pointer(std::size_t sprite, std::uint8_t pointer);
    void animate_first_buster(std::uint8_t frame);
    void move_all_sprites();
    void move_sprite(std::size_t sprite);
    [[nodiscard]] bool all_sprites_at_targets() const noexcept;

    const assets::Payload& payload_;
    DriveControlsState state_;
    BuildingControlsRegisters building_registers_;
    HeadquartersRegisters registers_;
    std::vector<std::uint8_t> scene_data_;
};

} // namespace ghostbusters::game
