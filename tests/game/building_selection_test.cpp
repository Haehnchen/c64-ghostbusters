#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/building_controls.hpp"
#include "game/building_entry.hpp"
#include "game/city_controls.hpp"
#include "game/drive_entry.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ghostbusters::game;

namespace {
void check(bool ok, const std::string& message)
{
    if (!ok) throw std::runtime_error(message);
}

void selection(const ghostbusters::assets::Payload& payload, unsigned row,
               unsigned column, bool below, bool haunted, bool drive)
{
    // Controlled handoff fixture, not a natural full-game parity claim.
    constexpr std::array<std::uint8_t, 4> road_y{0x42, 0x6A, 0x92, 0xBA};
    constexpr std::array<std::uint8_t, 4> building_x{0x2B, 0x47, 0x63, 0x7F};
    const auto selected = row * 4 + column + (below ? 4 : 0);
    const auto context = std::to_string(selected) + (below ? " below" : " above");
    CityControlsState initial;
    initial.sprites.x[0] = initial.sprites.x[1] = building_x[column];
    initial.sprites.y[0] = initial.sprites.y[1] = road_y[row];
    initial.map_types_ea28.fill(1);
    initial.owned_mask6d = 1;
    initial.empty_traps6b = 1;
    initial.backup_men3d = 3;
    initial.backpack_charge3e = 0xFF;
    initial.fire_latch11 = 0x10;
    initial.route_length66 = drive ? 1 : 0;
    initial.building_status_c8[selected] = haunted ? 0x1F : 0x10;
    CityControls city(payload, initial);
    city.tick({0, 1, static_cast<std::uint8_t>(below ? 0xED : 0xEF), 0});
    check(city.state().current_building6e == selected, "selection " + context);
    // HQ and Zuul have their own entry rules and no ordinary ghost placement.
    if (selected == 0x11 || selected == 0x0A) return;
    const auto statuses = city.state().building_status_c8;
    const auto verify = [&](const CityControlsState& state) {
        check(state.current_building6e == selected && state.building_status_c8 == statuses,
              "building/status handoff " + context);
    };
    const auto enter = [&](BuildingEntry& entry) {
        entry.tick();
        verify(entry.state().city);
        entry.tick();
        verify(entry.state().city);
        check(entry.transition() == BuildingEntryTransition::normal, "normal entry " + context);
        check(entry.sprites().pointers[4] != 0, "entry initially draws ghost " + context);
        BuildingControls controls(payload, entry.state(),
            {entry.state1e(), entry.scratch37(), {0, 0}, 0, {0, 0}});
        for (unsigned frame = 0; frame < 8; ++frame) {
            controls.tick({0, static_cast<std::uint8_t>(frame), 0xFF});
            verify(controls.state().city);
            check((controls.state().city.sprites.pointers[4] != 0) == haunted,
                  "ghost eligibility " + context);
        }
    };
    if (drive) {
        check(city.transition() == CityControlsTransition::drive, "drive selection " + context);
        DriveEntry drive_entry(payload, city.state(), {}, 0);
        drive_entry.tick();
        drive_entry.tick();
        verify(drive_entry.state());
        DriveControls controls(payload, drive_entry, {0, 0, 0x10, {0, 0}, {0, 0}});
        for (unsigned tick = 0; tick < 8192 &&
             controls.transition() == DriveControlsTransition::driving; ++tick) {
            (void)controls.tick({0, 0xFF, false});
            verify(controls.state().city);
        }
        check(controls.transition() == DriveControlsTransition::building, "drive finishes " + context);
        BuildingEntry entry(payload, controls, {0, 0, 0x1B});
        enter(entry);
    } else {
        check(city.transition() == CityControlsTransition::building, "direct selection " + context);
        BuildingEntry entry(payload, city.state(), 0,
                            DriveControlsPersistent(0, 0, 0, {0, 0}, {0, 0}),
                            {0, 0, 0x1B});
        enter(entry);
    }
}
}

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column)
                for (bool below : {false, true})
                    for (bool haunted : {false, true})
                        for (bool drive : {false, true})
                            selection(payload, row, column, below, haunted, drive);
        std::cout << "building selection and handoff tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
