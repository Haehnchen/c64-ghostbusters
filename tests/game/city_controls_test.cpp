#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/city_controls.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::CityControls;
using ghostbusters::game::CityControlsState;
using ghostbusters::game::CityControlsTransition;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

CityControlsState city_state()
{
    CityControlsState state;
    state.sprites.x[0] = state.sprites.x[1] = 0x47;
    state.sprites.y[0] = state.sprites.y[1] = 0xBA;
    for (std::size_t sprite = 4; sprite < 8; ++sprite) {
        state.sprites.x[sprite] = state.sprites.y[sprite] = 1;
        state.sprites.target_x[sprite] = state.sprites.target_y[sprite] = 2;
        state.shadow_target_x_ea56[sprite] = state.shadow_target_y_ea57[sprite] = 2;
    }
    return state;
}

void test_initial_hauntings_and_return(const Payload& payload)
{
    ghostbusters::game::EquipmentSelection shop(payload, {}, 0, {0, 0x60, 0}, 0, 0);
    ghostbusters::game::CityEntry entry(payload, shop);
    entry.tick();
    entry.tick();
    CityControls first_city(payload, entry, {1, 0, 0});
    // The original initialization at $9041-$904B supplies an immediately
    // visible ready site, a hidden ready site and an early detector-only site.
    std::array<std::uint8_t, 20> expected{};
    expected[7] = 0x1F;
    expected[8] = 0x1C;
    expected[15] = 0x14;
    check(first_city.state().building_status_c8 == expected,
          "new city must retain all three original startup hauntings");

    auto returning = first_city.state();
    returning.building_status_c8[7] = 0; // Already dealt with on this visit.
    returning.building_status_c8[8] = 0x5F;
    returning.building_status_c8[15] = 0x35;
    CityControls resumed(payload, returning);
    check(resumed.state().building_status_c8 == returning.building_status_c8,
          "city return must not recreate startup ghosts or reset their ages");
}

void test_capture_movement(const Payload& payload)
{
    CityControls city(payload, city_state());
    city.tick({0, 0, 0xFF, 0});
    check(city.state().sprites.x[0] == 0x47 && city.state().sprites.y[0] == 0xBA &&
              city.state().current_building6e == 0x0D && city.state().route_length66 == 1 &&
              city.state().last_map_cell1f == 0xDE,
          "idle capture checkpoint");

    for (unsigned i = 0; i < 16; ++i) city.tick({0, 0, 0xF7, 0});
    check(city.state().sprites.x[0] == 0x57 && city.state().sprites.x[1] == 0x57 &&
              city.state().sprites.y[0] == 0xBA && city.state().current_building6e == 0x0E &&
              city.state().route_length66 == 5 && city.state().last_map_cell1f == 0xE2,
          "right capture checkpoint");

    for (unsigned i = 0; i < 16; ++i) city.tick({0, 0, 0xFE, 0});
    check(city.state().sprites.x[0] == 0x55 && city.state().sprites.x[1] == 0x55 &&
              city.state().sprites.y[0] == 0xAA && city.state().current_building6e == 0x0D &&
              city.state().route_length66 == 7 && city.state().last_map_cell1f == 0x92,
          "up capture checkpoint");

    const auto before = city.state();
    city.tick({0, 0, 0xFF, 0});
    check(city.state().sprites.x == before.sprites.x && city.state().sprites.y == before.sprites.y &&
              city.state().current_building6e == before.current_building6e &&
              city.state().route_length66 == before.route_length66 &&
              city.state().last_map_cell1f == before.last_map_cell1f &&
              city.state().characters.screen == before.characters.screen &&
              city.state().characters.colors == before.characters.colors,
          "idle-after capture checkpoint");
    check(city.audio_events().empty(), "city movement emits no audio event");
}

void test_bait_boundaries(const Payload& payload)
{
    auto empty = city_state();
    CityControls empty_city(payload, empty);
    empty_city.tick({0, 0, 0xFF, 0x42});
    check(empty_city.state().key17 == 0 && empty_city.state().bait69 == 0 &&
              empty_city.state().notices.length4a() == 16,
          "empty bait is consumed as a key and queues notice 2");

    auto available = city_state();
    available.bait69 = 1;
    CityControls placed(payload, available);
    placed.tick({0, 0, 0xFF, 0x42});
    check(placed.state().bait69 == 0 && placed.state().bait_active68 == 1,
          "first bait activates and decrements stock");
    // The $DE low-byte cell is screen offset $2DE for this map row.
    check(placed.state().characters.screen[0x2DE] == 0x41 &&
              placed.state().characters.screen[0x2DF] == 0x42 &&
              placed.state().characters.colors[0x2DE] == 3 &&
              placed.state().characters.colors[0x2DF] == 3,
          "bait writes its two map cells and colors");

    auto active = city_state();
    active.bait69 = 2;
    active.bait_active68 = 1;
    active.sprites.target_x[4] = 0x77;
    CityControls already(payload, active);
    already.tick({0, 0, 0xFF, 0x42});
    check(already.state().bait69 == 1 && already.state().bait_active68 == 1 &&
              already.state().sprites.target_x[4] == 0x77,
          "active bait still consumes stock without replacing its formation");
}

void test_fire_boundaries(const Payload& payload)
{
    auto no_traps = city_state();
    no_traps.fire_latch11 = 0x10;
    CityControls traps(payload, no_traps);
    traps.tick({0, 0, 0xEF, 0});
    check(traps.transition() == CityControlsTransition::city &&
              traps.state().notices.length4a() == 41 && traps.state().route_length66 == 0,
          "fire without traps queues notice 0 and returns before trail");

    auto no_men = city_state();
    no_men.fire_latch11 = 0x10;
    no_men.empty_traps6b = 1;
    no_men.backup_men3d = 1;
    CityControls men(payload, no_men);
    men.tick({0, 0, 0xEF, 0});
    check(men.state().notices.length4a() == 55, "fire without backup men queues notice 1");

    auto discharged = city_state();
    discharged.fire_latch11 = 0x10;
    discharged.empty_traps6b = 1;
    discharged.backpack_charge3e = 0;
    CityControls packs(payload, discharged);
    packs.tick({0, 0, 0xEF, 0});
    check(packs.state().notices.length4a() == 38, "fire with discharged packs queues notice 3");

    auto driving = city_state();
    driving.fire_latch11 = 0x10;
    driving.empty_traps6b = 1;
    driving.route_length66 = 1;
    driving.map_types_ea28[0x0D] = 1;
    CityControls drive(payload, driving);
    drive.tick({0, 0, 0xEF, 0});
    check(drive.transition() == CityControlsTransition::drive, "nonempty route enters state 19");

    auto direct = driving;
    direct.route_length66 = 0;
    CityControls building(payload, direct);
    building.tick({0, 0, 0xEF, 0});
    check(building.transition() == CityControlsTransition::building, "empty route enters state 22");
}

void test_pk_alert_and_zuul(const Payload& payload)
{
    auto pk = city_state();
    pk.sprites.x[4] = pk.shadow_target_x_ea56[4] = 0x22;
    pk.sprites.y[4] = pk.shadow_target_y_ea57[4] = 0x33;
    CityControls arrival(payload, pk);
    arrival.tick({0, 1, 0xFF, 0});
    check(arrival.state().pk_high5b == 1 && arrival.state().pk_low5a == 0,
          "one roamer arrival adds exactly 100 PK energy");

    auto alert = city_state();
    alert.pending_alert80 = 5;
    alert.building_status_c8[5] = 0xF8;
    alert.bait_active68 = 1;
    CityControls marshmallow(payload, alert);
    marshmallow.tick({0, 1, 0xFF, 0});
    check(marshmallow.transition() == CityControlsTransition::marshmallow &&
              marshmallow.state().building_status_c8[5] == 0 &&
              marshmallow.state().bait_active68 == 0 &&
              marshmallow.state().notices.length4a() == 20,
          "mature alert enters state 36 and queues notice 4");

    auto poor = city_state();
    poor.sprites.x[2] = 0x61; poor.sprites.y[2] = 0x86;
    poor.sprites.x[3] = 0x65; poor.sprites.y[3] = 0x87;
    poor.starting_balance51 = {1, 0, 0};
    poor.balance57 = {0, 0x99, 0x99};
    CityControls loss(payload, poor);
    loss.tick({0, 1, 0xFF, 0});
    check(loss.transition() == CityControlsTransition::insufficient_balance &&
              loss.state().finale_active81 == 1,
          "poor Zuul rendezvous enters state 45");

    auto ready = poor;
    ready.balance57 = ready.starting_balance51;
    CityControls finale(payload, ready);
    finale.tick({0, 1, 0xFF, 0});
    check(finale.transition() == CityControlsTransition::city &&
              finale.state().finale_active81 == 1 && finale.state().countdown7c == 0xFF &&
              finale.state().move_mask3c == 0 && finale.state().notices.length4a() == 67,
          "funded Zuul rendezvous starts forced approach");

    auto enter = ready;
    enter.finale_active81 = 1;
    enter.countdown7c = 0;
    enter.sprites.x[0] = enter.sprites.x[1] = 0x63;
    enter.sprites.y[0] = enter.sprites.y[1] = 0x92;
    CityControls zuul(payload, enter);
    zuul.tick({0, 1, 0xFF, 0});
    check(zuul.transition() == CityControlsTransition::building &&
              zuul.state().current_building6e == 0x0A,
          "completed finale approach enters Zuul building");
    check(arrival.audio_events().empty() && marshmallow.audio_events().empty() &&
              loss.audio_events().empty() && finale.audio_events().empty() &&
              zuul.audio_events().empty(),
          "PK, marshmallow, and Zuul branches emit no audio event");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_initial_hauntings_and_return(payload);
        test_capture_movement(payload);
        test_bait_boundaries(payload);
        test_fire_boundaries(payload);
        test_pk_alert_and_zuul(payload);
        std::cout << "native city-controls tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
