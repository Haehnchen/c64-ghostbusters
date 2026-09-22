#pragma once

#include "assets/payload.hpp"
#include "game/building_controls.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// The two return handlers retain the same bytes as BuildingControls.  The
// state itself is copied at the native scene boundary so that the caller can
// keep the control scene alive while the busters leave it.
struct BuildingReturnInput {
    std::uint8_t frame09 = 0;
};

enum class BuildingReturnTransition : std::uint8_t {
    city = 0x11,
    move_to_trap = 0x20,
    move_offscreen = 0x21,
};

// Native boundary for State $20 at $8875-$8895 and State $21 at
// $8896-$88B5.  Both handlers execute one original state handler per tick.
class BuildingReturn {
public:
    BuildingReturn(const assets::Payload& payload, DriveControlsState state,
                   BuildingControlsRegisters registers);
    BuildingReturn(const assets::Payload& payload,
                   const BuildingControls& controls);

    void tick(BuildingReturnInput input = {});

    [[nodiscard]] DriveControlsState& state() noexcept { return state_; }
    [[nodiscard]] const DriveControlsState& state() const noexcept { return state_; }
    [[nodiscard]] BuildingControlsRegisters& registers() noexcept { return registers_; }
    [[nodiscard]] const BuildingControlsRegisters& registers() const noexcept
    {
        return registers_;
    }
    [[nodiscard]] BuildingReturnTransition transition() const noexcept
    {
        return static_cast<BuildingReturnTransition>(state_.city.state3a);
    }

private:
    void move_all_sprites();
    void move_sprite(std::size_t sprite);
    void set_pointer(std::size_t sprite, std::uint8_t pointer);
    void animate_pair(std::size_t sprite, std::size_t animation,
                      std::uint8_t settled_pointer, std::uint8_t frame);
    void animate_both(std::uint8_t settled_pointer, std::uint8_t frame);
    [[nodiscard]] bool sprite_at_target(std::size_t sprite) const noexcept;
    void position_outer_pair();

    const assets::Payload& payload_;
    DriveControlsState state_;
    BuildingControlsRegisters registers_;
    std::vector<std::uint8_t> scene_data_;
};

} // namespace ghostbusters::game
