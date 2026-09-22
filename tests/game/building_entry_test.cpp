#include "assets/payload.hpp"
#include "game/building_entry.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::BuildingEntry;
using ghostbusters::game::BuildingEntryRegisters;
using ghostbusters::game::BuildingEntryTransition;
using ghostbusters::game::CityControlsState;
using ghostbusters::game::DriveControlsPersistent;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

CityControlsState make_city(std::uint8_t building)
{
    CityControlsState city;
    city.state3a = 0x16;
    city.current_building6e = building;
    city.route_length66 = 0x42;
    city.key17 = 0x51;
    city.countdown7c = 0x33;
    city.characters.screen.fill(0xA1);
    city.characters.colors.fill(0xB2);
    city.characters.charset.fill(0xC3);
    city.characters.background = 4;
    city.characters.multicolor1 = 5;
    city.characters.multicolor2 = 6;
    city.sprites.x.fill(0x71);
    city.sprites.y.fill(0x72);
    city.sprites.target_x.fill(0x73);
    city.sprites.target_y.fill(0x74);
    city.sprites.pointers.fill(0x0C);
    city.sprites.colors.fill(0x0D);
    city.sprites.priority_mask = 0xF0;
    city.sprites.x_expand_mask = 0xAA;
    city.sprites.y_expand_mask = 0x55;
    city.sprites.multicolor_mask = 0x11;
    city.roamer_counters6f.fill(0x81);
    return city;
}

void check_state_22(const Payload& payload)
{
    BuildingEntry entry(payload, make_city(1), 2,
                        DriveControlsPersistent(0, 0x81, 0, {0, 0}, {0, 0}),
                        BuildingEntryRegisters(0xA4, 0xB5, 0x1B));
    entry.tick();

    check(entry.stage() == BuildingEntry::Stage::preparing, "State 22 stage");
    check(entry.state().city.state3a == 0x17, "State 22 transition");
    check(entry.state().animation65 == 0, "State 22 animation clear");
    check(entry.state().city.route_length66 == 0, "State 22 route clear");
    check(entry.scratch37() == 0, "State 22 scratch $37 clear");
    check(entry.state1e() == 0xA4 && entry.vic_control1() == 0x1B,
          "State 22 retained registers");
    check(std::all_of(entry.sprites().x.begin(), entry.sprites().x.end(),
                      [](auto value) { return value == 0; }) &&
          std::all_of(entry.sprites().y.begin(), entry.sprites().y.end(),
                      [](auto value) { return value == 0; }),
          "State 22 current coordinates clear");
    check(entry.sprites().target_x[0] == 0x73 && entry.sprites().target_y[7] == 0x74,
          "State 22 targets retained");
    check(std::all_of(entry.state().city.roamer_counters6f.begin(),
                      entry.state().city.roamer_counters6f.end(),
                      [](auto value) { return value == 0; }),
          "State 22 roamer counters clear");

    const auto& frame = entry.characters();
    check(std::all_of(frame.screen.begin(), frame.screen.begin() + 0x3C0,
                      [](auto value) { return value == 0; }) &&
          frame.screen[0x3C0] == 0xA1,
          "State 22 screen clear boundary");
    check(std::all_of(frame.colors.begin(), frame.colors.begin() + 0x370,
                      [](auto value) { return value == 9; }),
          "State 22 scene colors");
    check(std::all_of(frame.colors.begin() + 0x370, frame.colors.begin() + 0x397,
                      [](auto value) { return value == 1; }) &&
          std::all_of(frame.colors.begin() + 0x397, frame.colors.begin() + 0x3C0,
                      [](auto value) { return value == 0; }) &&
          frame.colors[0x3C0] == 0xB2,
          "State 22 status and tail colors");
    check(frame.background == 0x0C && frame.multicolor1 == 0 &&
          frame.multicolor2 == 6, "State 22 VIC colors");
}

void check_normal_scene(const Payload& payload)
{
    const auto screen = payload.asset("buildings/normal/screen");
    const auto charset = payload.asset("buildings/normal/charset");
    const auto colors = payload.asset("buildings/normal/colors");
    const auto low_tiles = payload.asset("buildings/overlays/low/1/tiles");
    const auto low_charset = payload.asset("buildings/overlays/low/1/charset");
    const auto high_screen = payload.asset("buildings/overlays/high/2/screen");
    const auto high_charset = payload.asset("buildings/overlays/high/2/charset");
    const auto descriptor_patch =
        payload.asset("buildings/descriptors/1/charset_patch");
    const auto vehicle_charset =
        payload.asset("buildings/vehicles/wagon/charset");
    // Descriptor $97 selects replacement color 13, patch 1, low overlay 1
    // and high overlay 2, exercising every generic composition layer.
    BuildingEntry entry(payload, make_city(1), 2,
                        DriveControlsPersistent(0, 0x81, 0, {0, 0}, {0, 0}),
                        BuildingEntryRegisters(0, 0xCC, 0x9B));
    entry.tick();
    entry.tick();

    check(entry.stage() == BuildingEntry::Stage::ready, "State 23 stage");
    check(entry.transition() == BuildingEntryTransition::normal &&
          entry.state().city.countdown7c == 0x7F, "normal transition");
    check(entry.state1e() == 1 && entry.vic_control1() == 0x8B &&
          !entry.display_enabled(), "State 23 register setup");
    check(entry.state().city.key17 == 0 && entry.audio_events().empty(),
          "State 23 key/audio contract");

    const auto& frame = entry.characters();
    check(frame.screen[0] == screen[0] &&
          frame.screen[0x300] == screen[0x300],
          "generic background page order: " + std::to_string(frame.screen[0]) + "/" +
              std::to_string(screen[0]) + ", " +
              std::to_string(frame.screen[0x300]) + "/" +
              std::to_string(screen[0x300]));
    check(frame.charset[0x260] == charset[0x60],
          "generic building charset source");
    check(frame.colors[0] == (colors[0] & 0x0F) &&
          frame.colors[4] == 0x0D,
          "color RAM nibble and exact-$0A replacement");
    check(frame.screen[0x30] == low_tiles[0] &&
          frame.screen[0x30 + 18] == low_tiles[0],
          "low overlay repetition");
    check(std::equal(frame.charset.begin() + 0x330,
                     frame.charset.begin() + 0x3A0,
                     low_charset.begin()),
          "low overlay charset");
    check(frame.screen[0x120] == high_screen[0] &&
          frame.screen[0x120 + 9 * 40 + 21] == high_screen[219] &&
          frame.colors[0x120] == 9,
          "high overlay screen and color");
    check(std::equal(frame.charset.begin() + 0x3D0,
                     frame.charset.begin() + 0x5D0,
                     high_charset.begin()),
          "high overlay charset");
    check(std::equal(frame.charset.begin() + 0x268,
                     frame.charset.begin() + 0x278,
                     descriptor_patch.begin()),
          "descriptor charset patch");
    check(std::equal(frame.charset.begin() + 0x200,
                     frame.charset.begin() + 0x260,
                     vehicle_charset.begin()),
          "vehicle charset restore");
    check(frame.screen[0x2F2] == 0x40 && frame.screen[0x31A] == 0x44 &&
          frame.screen[0x342] == 0x48,
          "vehicle screen codes");
    constexpr auto vehicle_color = static_cast<std::uint8_t>(0x06U | 8U);
    check(frame.colors[0x2F2] == vehicle_color && frame.multicolor2 == 0x0B,
          "vehicle/VIC colors");

    check(entry.sprites().pointers ==
              std::array<std::uint8_t, 8>{0x19, 0x19, 0x19, 0x19,
                                          0x0A, 0x0E, 0x0E, 0x37},
          "building sprite pointers");
    check(entry.sprites().x ==
              std::array<std::uint8_t, 8>{0, 0, 0, 0, 0x50, 0xA8, 0xA8, 0xB0} &&
          entry.sprites().y ==
              std::array<std::uint8_t, 8>{0, 0, 0, 0, 0x50, 0xC7, 0xC7, 0xBA},
          "building current coordinates");
    check(entry.sprites().target_x ==
              std::array<std::uint8_t, 8>{0, 0, 0, 0, 0x50, 0x90, 0xA8, 0x98} &&
          entry.sprites().target_y ==
              std::array<std::uint8_t, 8>{0, 0, 0, 0, 0x50, 0xC7, 0xC7, 0xBA},
          "building target coordinates");
    check(entry.sprites().priority_mask == 0 && entry.sprites().x_expand_mask == 0 &&
          entry.sprites().y_expand_mask == 0 && entry.sprites().multicolor_mask == 0xEF,
          "building sprite VIC masks");
    check(std::all_of(entry.sprites().colors.begin(), entry.sprites().colors.end(),
                      [](auto value) { return value == 0x0D; }),
          "State 23 does not write sprite colors");

    const auto before = entry.state().city.state3a;
    entry.tick();
    check(entry.state().city.state3a == before, "ready tick is inert");
}

void check_special_scenes(const Payload& payload)
{
    const auto screen = payload.asset("buildings/zuul/screen");
    const auto charset = payload.asset("buildings/zuul/charset");
    const auto colors = payload.asset("buildings/zuul/colors");
    BuildingEntry zuul(payload, make_city(0x0A), 0,
                       DriveControlsPersistent(0, 0, 0, {0, 0}, {0, 0}),
                       BuildingEntryRegisters(0, 0, 0x1B));
    zuul.tick();
    zuul.tick();
    check(zuul.transition() == BuildingEntryTransition::zuul,
          "Zuul transition");
    check(zuul.characters().screen[0] == screen[0] &&
          zuul.characters().screen[0x300] == screen[0x300],
          "Zuul background source");
    check(zuul.characters().charset[0x260] == charset[0x60],
          "Zuul charset source");
    check(zuul.characters().colors[0x0FF] == (colors[0x100] & 0x0F) &&
          zuul.characters().colors[0x1FF] == (colors[0x200] & 0x0F),
          "color page-join duplicate stores");
    check(zuul.state().city.countdown7c == 0x33,
          "Zuul leaves countdown unchanged");

    BuildingEntry headquarters(payload, make_city(0x11), 3,
                               DriveControlsPersistent(0, 0, 0, {0, 0}, {0, 0}),
                               BuildingEntryRegisters(0, 0, 0x1B));
    headquarters.tick();
    headquarters.tick();
    check(headquarters.transition() ==
              BuildingEntryTransition::ghostbusters_headquarters,
          "headquarters transition");
    const std::array<std::uint8_t, 12> sign{
        7, 8, 15, 19, 20, 2, 21, 19, 20, 5, 18, 19,
    };
    check(std::equal(sign.begin(), sign.end(),
                     headquarters.characters().screen.begin() + 0x14D) &&
          std::all_of(headquarters.characters().colors.begin() + 0x14D,
                      headquarters.characters().colors.begin() + 0x159,
                      [](auto value) { return value == 2; }),
          "headquarters sign");
    check(headquarters.state().city.countdown7c == 0x33,
          "headquarters leaves countdown unchanged");
}

void check_guards(const Payload& payload)
{
    auto city = make_city(0);
    city.state3a = 0x15;
    bool wrong_state = false;
    try {
        BuildingEntry entry(payload, city, 0,
                            DriveControlsPersistent(0, 0, 0, {0, 0}, {0, 0}),
                            BuildingEntryRegisters(0, 0, 0x1B));
    } catch (const std::invalid_argument&) {
        wrong_state = true;
    }
    check(wrong_state, "wrong state guard");

    bool wrong_vehicle = false;
    try {
        BuildingEntry entry(payload, make_city(0), 4,
                            DriveControlsPersistent(0, 0, 0, {0, 0}, {0, 0}),
                            BuildingEntryRegisters(0, 0, 0x1B));
    } catch (const std::out_of_range&) {
        wrong_vehicle = true;
    }
    check(wrong_vehicle, "vehicle guard");
}

void check_direct_persistent(const Payload& payload)
{
    const DriveControlsPersistent persistent(0xA5, 0x81, 0x10,
                                              {0xB6, 0xC7}, {0xD8, 0xE9},
                                              0x1F, 0x22, 0x03, 0x64, 0x2D);
    BuildingEntry entry(payload, make_city(1), 2, persistent,
                        BuildingEntryRegisters(0, 0, 0x1B));

    check(entry.state().direction64 == persistent.direction64 &&
          entry.state().animation65 == persistent.animation65 &&
          entry.state().scroll_position1a == persistent.retained1a &&
          entry.state().speed1b == persistent.retained1b &&
          entry.state().scroll_fraction1c == persistent.retained1c &&
          entry.state().vehicle_position63 == persistent.retained63 &&
          entry.state().distance67 == persistent.retained67 &&
          entry.state().fire_latch13 == persistent.fire_latch13 &&
          entry.state().roamer_source73 == persistent.roamer_source73 &&
          entry.state().capture_timer75 == persistent.capture_timer75,
          "direct entry retains drive persistent bytes");

    entry.tick();
    check(entry.state().animation65 == 0 &&
          entry.state().direction64 == persistent.direction64 &&
          entry.state().scroll_position1a == persistent.retained1a &&
          entry.state().speed1b == persistent.retained1b &&
          entry.state().scroll_fraction1c == persistent.retained1c &&
          entry.state().vehicle_position63 == persistent.retained63 &&
          entry.state().distance67 == persistent.retained67 &&
          entry.state().fire_latch13 == persistent.fire_latch13 &&
          entry.state().roamer_source73 == persistent.roamer_source73 &&
          entry.state().capture_timer75 == persistent.capture_timer75,
          "State 22 clears animation and retains drive persistent bytes");

    entry.tick();
    check(entry.state().animation65 == 0 &&
          entry.state().direction64 == persistent.direction64 &&
          entry.state().scroll_position1a == persistent.retained1a &&
          entry.state().speed1b == persistent.retained1b &&
          entry.state().scroll_fraction1c == persistent.retained1c &&
          entry.state().vehicle_position63 == persistent.retained63 &&
          entry.state().distance67 == persistent.retained67 &&
          entry.state().fire_latch13 == persistent.fire_latch13 &&
          entry.state().roamer_source73 == persistent.roamer_source73 &&
          entry.state().capture_timer75 == persistent.capture_timer75,
          "State 23 retains drive persistent bytes");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        check_state_22(payload);
        check_normal_scene(payload);
        check_special_scenes(payload);
        check_guards(payload);
        check_direct_persistent(payload);
        std::cout << "building entry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
