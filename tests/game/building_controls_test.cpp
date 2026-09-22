#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/building_controls.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::BuildingControls;
using ghostbusters::game::BuildingControlsInput;
using ghostbusters::game::BuildingControlsRegisters;
using ghostbusters::game::DriveControlsState;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

DriveControlsState building_state(std::uint8_t handler)
{
    DriveControlsState state;
    state.city.state3a = handler;
    state.city.current_building6e = 3;
    state.city.building_status_c8[3] = 0x0C;
    state.city.countdown7c = 0x7F;
    state.city.fire_latch11 = 0x10;
    state.city.key17 = 0x91;
    state.city.owned_mask6d = 0x02;
    for (std::size_t i = 0; i < 8; ++i) {
        state.city.sprites.pointers[i] = 0x0E;
        state.city.sprites.x[i] = state.city.sprites.target_x[i] =
            static_cast<std::uint8_t>(0x50 + i * 4);
        state.city.sprites.y[i] = state.city.sprites.target_y[i] =
            static_cast<std::uint8_t>(0xB0 + i);
    }
    return state;
}

BuildingControlsRegisters registers(std::array<std::uint8_t, 2> animation = {0, 0},
                                     std::uint8_t direction = 0,
                                     std::array<std::uint8_t, 2> beams = {0x33, 0x44})
{
    return BuildingControlsRegisters(0xA1, 0xB2, animation, direction, beams);
}

void test_state_18_controls_and_fire(const Payload& payload)
{
    auto state = building_state(0x18);
    state.city.sprites.x[0] = 0x10;
    state.city.sprites.target_x[0] = 0x13;
    state.city.sprites.target_x[5] = 0x20;
    state.city.sprites.target_y[5] = 0xAA;
    state.city.sprites.x[5] = 0x60;
    state.city.sprites.y[5] = 0xB8;
    BuildingControls controls(payload, state, registers({6, 7}));
    controls.tick({0, 0, 0xFA}); // left, up and no fire edge

    const auto& after = controls.state();
    check(after.city.state3a == 0x18, "State 18 remains active without a fire edge");
    check(after.city.sprites.x[0] == 0x11,
          "$9AE3 moves every sprite by one pixel toward its target");
    check(after.city.sprites.target_x[5] == 0x20 &&
              after.city.sprites.target_y[5] == 0xAA,
          "$9AEF/$9B14 preserve inclusive lower clamps after joystick movement");
    const auto outer_x = (controls.registers().animation77[0] & 1U) != 0
                             ? static_cast<std::uint8_t>(after.city.sprites.x[5] - 6)
                             : static_cast<std::uint8_t>(after.city.sprites.x[5] + 6);
    check(after.city.sprites.target_x[7] == outer_x &&
              after.city.sprites.target_y[7] ==
                  static_cast<std::uint8_t>(after.city.sprites.y[5] - 7),
          "$9A16 positions sprite 7 from sprite 5 and facing bit");
    check(controls.registers().state1e == 0xA1 &&
              controls.registers().scratch37 == 0xB2,
          "States 18-1A retain explicit $1E/$37 bytes");

    auto fire_state = building_state(0x18);
    fire_state.city.sprites.y[5] = 0xBC;
    BuildingControls fire(payload, fire_state, registers());
    fire.tick({0, 1, 0xEF});
    check(fire.state().city.state3a == 0x19 && fire.state().city.key17 == 0,
          "new active-low fire press advances State 18 to State 19");
    check(fire.state().city.sprites.target_y[7] ==
              static_cast<std::uint8_t>(fire.state().city.sprites.y[5] + 1),
          "State 18 fire exit places sprite 7 target one row below sprite 5");
}

void test_ghost_update_and_timeout(const Payload& payload)
{
    auto moving = building_state(0x19);
    moving.city.sprites.x[4] = moving.city.sprites.target_x[4] = 0x8F;
    moving.city.sprites.y[4] = moving.city.sprites.target_y[4] = 0x40;
    moving.city.sprites.priority_mask = 0xFF;
    moving.city.owned_mask6d = 0;
    BuildingControls ghost(payload, moving, registers({}, 0));
    ghost.tick({1, 0, 0xFF});
    check(ghost.state().city.sprites.x[4] == 0x8E &&
              ghost.state().city.sprites.target_x[4] == 0x8E,
          "$9972 clamps the moving ghost at the right building boundary");
    check(ghost.registers().direction7d == 5 &&
              ghost.state().city.sprites.pointers[4] == 0x0B,
          "boundary turn and periodic random turn/pointer occur in original order");
    check((ghost.state().city.sprites.priority_mask & 0x10) != 0,
          "$9AA6 puts sprite 4 behind foreground when equipment bit 1 is absent");

    auto timeout = building_state(0x1A);
    timeout.city.building_status_c8[3] = 0;
    timeout.city.countdown7c = 0;
    timeout.city.sprites.x[7] = 0x41;
    timeout.city.sprites.y[7] = 0x55;
    timeout.city.sprites.target_x[7] = 0x41;
    timeout.city.sprites.target_y[7] = 0x55;
    BuildingControls expired(payload, timeout, registers());
    expired.tick({0, 1, 0xFF});
    check(expired.state().city.state3a == 0x20 && expired.state().city.key17 == 0,
          "$9A8A unwinds into $8823 and completes the same-frame State-20 transition");
    check(expired.state().city.sprites.pointers[4] == 0,
          "unresolved-building timeout check clears sprite pointer $2F");
    check(expired.state().city.sprites.target_x[5] == 0x41 &&
              expired.state().city.sprites.target_x[6] == 0x53 &&
              expired.state().city.sprites.target_y[5] == 0x55 &&
              expired.state().city.sprites.target_y[6] == 0x55,
          "$8823 derives both cleanup targets from sprite 7");
}

void test_states_19_and_1a_fire(const Payload& payload)
{
    auto second = building_state(0x19);
    second.city.sprites.pointers[5] = 0x0F;
    BuildingControls state19(payload, second, registers({1, 0}));
    state19.tick({0, 1, 0xEF});
    check(state19.state().city.state3a == 0x1A &&
              state19.state().city.sprites.pointers[5] == 0x17,
          "State 19 fire preserves facing bit and selects pointer $16/$17");
    check(state19.state().city.sprites.target_x[6] == 144 &&
              state19.state().city.sprites.target_y[6] == 199,
          "State 19 fire loads the exact sprite-6 setup target");

    auto trap = building_state(0x1A);
    trap.city.sprites.x[4] = trap.city.sprites.target_x[4] = 0x60;
    trap.city.sprites.y[4] = trap.city.sprites.target_y[4] = 0x50;
    trap.city.sprites.x[5] = trap.city.sprites.target_x[5] = 0x50;
    trap.city.sprites.y[5] = trap.city.sprites.target_y[5] = 0x60;
    trap.city.sprites.pointers[5] = 0x0F;
    trap.city.sprites.x[6] = trap.city.sprites.target_x[6] = 0x70;
    trap.city.sprites.y[6] = trap.city.sprites.target_y[6] = 0x40;
    BuildingControls state1a(payload, trap, registers({}, 2));
    state1a.tick({0, 1, 0xEF});
    check(state1a.state().city.state3a == 0x1B &&
              state1a.state().city.sprites.pointers[6] == 0x16,
          "State 1A fire selects trap pointer and hands off to State 1B");
    check(state1a.registers().beam_direction7e == std::array<std::uint8_t, 2>{0, 0xFF},
          "$98EB/$9918 retain both byte-exact beam direction results");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_state_18_controls_and_fire(payload);
        test_ghost_update_and_timeout(payload);
        test_states_19_and_1a_fire(payload);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) return 1;
    std::cout << "building controls tests passed\n";
    return 0;
}
