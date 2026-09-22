#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/headquarters.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::BuildingControlsRegisters;
using ghostbusters::game::DriveControlsState;
using ghostbusters::game::Headquarters;
using ghostbusters::game::HeadquartersRegisters;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

DriveControlsState headquarters_state(std::uint8_t handler)
{
    DriveControlsState state;
    state.city.state3a = handler;
    state.city.key17 = 0x66;
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        state.city.sprites.pointers[sprite] = static_cast<std::uint8_t>(0x0E + sprite);
        state.city.sprites.x[sprite] = static_cast<std::uint8_t>(0x20 + sprite);
        state.city.sprites.y[sprite] = static_cast<std::uint8_t>(0x40 + sprite);
        state.city.sprites.target_x[sprite] = static_cast<std::uint8_t>(0x60 + sprite);
        state.city.sprites.target_y[sprite] = static_cast<std::uint8_t>(0x80 + sprite);
        state.city.sprites.colors[sprite] = static_cast<std::uint8_t>(sprite + 1);
    }
    state.city.sprites.enabled_mask = 0xA5;
    state.city.sprites.priority_mask = 0x3C;
    return state;
}

BuildingControlsRegisters building_registers()
{
    return BuildingControlsRegisters(0xA1, 0xB2, {0x07, 0xE8}, 0xC3,
                                     {0xD4, 0xE5});
}

void test_state34_restores_inventory_and_first_buster(const Payload& payload)
{
    auto state = headquarters_state(0x22);
    state.city.empty_traps6b = 0xEE;
    state.city.backup_men3d = 1;
    state.city.backpack_charge3e = 0x12;
    Headquarters headquarters(payload, state, building_registers(),
                               HeadquartersRegisters(6, 4, 0x91));
    headquarters.tick();

    const auto& result = headquarters.state();
    const auto& sprites = result.city.sprites;
    check(result.city.state3a == 0x23 && result.city.key17 == 0,
          "$8D86 clears key $17 and advances State 34 to State 35");
    check(headquarters.registers().total_traps6a == 6 &&
              headquarters.registers().full_traps6c == 0 &&
              result.city.empty_traps6b == 6,
          "State 34 empties every purchased trap without changing the total");
    check(result.city.backup_men3d == 3 && result.city.backpack_charge3e == 0x99,
          "State 34 restores three men and packed-BCD charge 99");
    check(headquarters.building_registers().animation77[0] == 0 &&
              headquarters.building_registers().animation77[1] == 0xE8,
          "$95A5 clears $77 but preserves adjacent animation byte $78");

    bool reset_exactly = true;
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        const bool first = sprite == 5;
        reset_exactly &= sprites.pointers[sprite] == 0;
        reset_exactly &= sprites.x[sprite] == (first ? 0x5A : 0);
        reset_exactly &= sprites.y[sprite] == (first ? 0xA8 : 0);
        reset_exactly &= sprites.target_x[sprite] == (first ? 0xCF : 0);
        reset_exactly &= sprites.target_y[sprite] == (first ? 0xC7 : 0);
        reset_exactly &= sprites.colors[sprite] ==
                         0;
    }
    check(reset_exactly,
          "$95A5 clears pointers and coordinate tables before installing sprite 5");
    check(sprites.enabled_mask == 0xA5 && sprites.priority_mask == 0x3C,
          "$95A5 preserves VIC masks while the IRQ refreshes pointer-derived colors");
    check(headquarters.registers().scratch23 == 0x91,
          "State 34 does not touch CPU scratch byte $23");
}

void settle_all(DriveControlsState& state, std::uint8_t coordinate)
{
    auto& sprites = state.city.sprites;
    sprites.x.fill(coordinate);
    sprites.y.fill(coordinate);
    sprites.target_x.fill(coordinate);
    sprites.target_y.fill(coordinate);
}

void test_state35_animates_moves_and_spawns_in_order(const Payload& payload)
{
    auto state = headquarters_state(0x23);
    settle_all(state, 0x20);
    auto& sprites = state.city.sprites;
    sprites.x[5] = 0x6F;
    sprites.target_x[5] = 0xCF;
    sprites.x[6] = sprites.y[6] = sprites.target_x[6] = sprites.target_y[6] = 0;
    sprites.x[7] = sprites.y[7] = sprites.target_x[7] = sprites.target_y[7] = 0;
    auto active_registers = building_registers();
    active_registers.animation77[0] = 0;
    Headquarters headquarters(payload, state, active_registers,
                               HeadquartersRegisters(5, 2, 0xFE));
    headquarters.tick({0});

    const auto& after = headquarters.state().city.sprites;
    check(after.x[5] == 0x70 && after.x[6] == 0x5A && after.y[6] == 0xA8 &&
              after.target_x[6] == 0xA8 && after.target_y[6] == 0xC7,
          "sprite 5 reaching X=$70 spawns sprite 6 with the original coordinates");
    check(after.x[7] == 0 && after.target_x[7] == 0,
          "the loop cannot spawn sprites 6 and 7 in the same tick");
    check(headquarters.building_registers().animation77[0] == 2 &&
              after.pointers[5] == 0x10 && after.pointers[6] == 0x10 &&
              after.pointers[7] == 0x10,
          "State 35 animates sprite 5 and copies its pointer to sprites 6 and 7");
    check(headquarters.registers().scratch23 == 1,
          "$9AE3 leaves scratch $23 at one after one-pixel movement");

    auto second_state = headquarters.state();
    auto& second_sprites = second_state.city.sprites;
    second_sprites.x[5] = 0x71;
    second_sprites.x[6] = 0x6F;
    second_sprites.target_x[6] = 0xA8;
    second_sprites.x[7] = second_sprites.y[7] = 0;
    second_sprites.target_x[7] = second_sprites.target_y[7] = 0;
    Headquarters second(payload, second_state, headquarters.building_registers(),
                        headquarters.registers());
    second.tick({1});
    check(second.state().city.sprites.x[7] == 0x5A &&
              second.state().city.sprites.y[7] == 0xA8 &&
              second.state().city.sprites.target_x[7] == 0xA8 &&
              second.state().city.sprites.target_y[7] == 0xC7,
          "sprite 6 reaching X=$70 spawns sprite 7 on a later tick");
}

void test_state35_waits_for_every_sprite_and_returns_to_city(const Payload& payload)
{
    auto waiting = headquarters_state(0x23);
    settle_all(waiting, 0x20);
    waiting.city.sprites.target_x[0] = 0x22;
    Headquarters not_ready(payload, waiting, building_registers(),
                           HeadquartersRegisters(3, 1, 0x44));
    not_ready.tick({1});
    check(not_ready.state().city.state3a == 0x23 &&
              not_ready.state().city.sprites.x[0] == 0x21,
          "$8910 checks all eight current/target coordinate pairs");

    auto ready = headquarters_state(0x23);
    settle_all(ready, 0x20);
    Headquarters complete(payload, ready, building_registers(),
                          HeadquartersRegisters(3, 0, 0x44));
    complete.tick({1});
    check(complete.state().city.state3a == 0x11,
          "State 35 returns to city redraw State 17 once all sprites are settled");
    check(complete.state().city.key17 == 0x66,
          "the direct State 35 city handoff preserves key $17");
    check(complete.registers().scratch23 == 0,
          "a settled State 35 tick leaves its explicit scratch clear intact");
}

void test_state_validation(const Payload& payload)
{
    auto state = headquarters_state(0x21);
    try {
        Headquarters invalid(payload, state, building_registers(),
                             HeadquartersRegisters(1, 0, 0));
        (void)invalid;
        check(false, "constructor rejects handlers outside States 34/35");
    } catch (const std::invalid_argument&) {
        check(true, "constructor rejects handlers outside States 34/35");
    }
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_state34_restores_inventory_and_first_buster(payload);
        test_state35_animates_moves_and_spawns_in_order(payload);
        test_state35_waits_for_every_sprite_and_returns_to_city(payload);
        test_state_validation(payload);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
    if (failures == 0) std::cout << "headquarters tests passed\n";
    return failures == 0 ? 0 : 1;
}
