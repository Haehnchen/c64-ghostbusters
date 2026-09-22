#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/drive_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::DriveControlsState;
using ghostbusters::game::update_drive_frame;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

DriveControlsState patterned_state()
{
    DriveControlsState state;
    state.city.state3a = 0x15;
    state.vehicle5c = 2;
    for (std::size_t i = 0; i < state.city.characters.screen.size(); ++i) {
        state.city.characters.screen[i] = static_cast<std::uint8_t>(i ^ (i >> 8U));
        state.city.characters.colors[i] = static_cast<std::uint8_t>(i & 0x0F);
    }
    for (std::size_t i = 0; i < state.city.characters.charset.size(); ++i) {
        state.city.characters.charset[i] = static_cast<std::uint8_t>(i * 3U);
    }
    return state;
}

void test_left_boundary(const Payload& payload)
{
    auto state = patterned_state();
    const auto before = state.city.characters;
    state.vehicle_position63 = 0x40;
    state.direction64 = 0xFF;
    std::uint8_t d016 = 0xD9;
    update_drive_frame(payload, state, d016);

    check(state.vehicle_position63 == 0x3F && state.direction64 == 0,
          "left frame consumes one negative direction unit");
    check(d016 == 0xDE && state.city.sprites.x[2] == 0x5D &&
              state.city.sprites.x[3] == 0x5D && state.city.sprites.y[2] == 0x84 &&
              state.city.sprites.y[3] == 0x84,
          "left frame updates fine scroll and both vehicle sprites");
    for (std::size_t row = 0; row < 16; ++row) {
        const auto base = 0xA0U + row * 0x28U;
        for (std::size_t column = 0; column < 13; ++column) {
            check(state.city.characters.screen[base + 14 + column] ==
                      before.screen[base + 15 + column],
                  "left helper copies all 13 columns of each row");
        }
        check(state.city.characters.screen[base + 13] == before.screen[base + 13] &&
                  state.city.characters.screen[base + 27] == before.screen[base + 27],
              "left helper preserves cells outside its destination window");
    }
    check(state.city.characters.colors == before.colors &&
              state.city.characters.charset == before.charset,
          "drive grid helper changes neither colors nor charset bytes");
}

void test_right_boundary(const Payload& payload)
{
    auto state = patterned_state();
    const auto before = state.city.characters.screen;
    state.vehicle_position63 = 0x3F;
    state.direction64 = 1;
    std::uint8_t d016 = 0xDD;
    update_drive_frame(payload, state, d016);

    check(state.vehicle_position63 == 0x40 && state.direction64 == 0 && d016 == 0xD8,
          "right frame crosses the character boundary and clears fine scroll");
    for (std::size_t row = 0; row < 16; ++row) {
        const auto base = 0x9FU + row * 0x28U;
        for (std::size_t column = 0; column < 13; ++column) {
            check(state.city.characters.screen[base + 17 + column] ==
                      before[base + 16 + column],
                  "right helper copies all 13 columns in overlap-safe order");
        }
        check(state.city.characters.screen[base + 15] == before[base + 15] &&
                  state.city.characters.screen[base + 30] == before[base + 30],
              "right helper preserves cells outside its destination window");
    }
}

void test_subcharacter_and_state_gate(const Payload& payload)
{
    auto state = patterned_state();
    const auto screen = state.city.characters.screen;
    state.vehicle_position63 = 0x40;
    state.direction64 = 2;
    std::uint8_t d016 = 0xD8;
    update_drive_frame(payload, state, d016);
    check(state.vehicle_position63 == 0x41 && state.direction64 == 1 &&
              state.city.characters.screen == screen && d016 == 0xDA,
          "non-boundary motion changes one pixel without copying cells");

    state = patterned_state();
    state.city.state3a = 0x14;
    state.vehicle_position63 = 0x40;
    state.direction64 = 0xFF;
    state.city.sprites.x[2] = 0x33;
    state.city.sprites.y[2] = 0x44;
    d016 = 0xD9;
    update_drive_frame(payload, state, d016);
    check(state.vehicle_position63 == 0x3F && state.direction64 == 0 &&
              state.city.characters.screen != screen,
          "direction and grid helper execute before the State-21 gate");
    check(d016 == 0xD9 && state.city.sprites.x[2] == 0x33 &&
              state.city.sprites.y[2] == 0x44,
          "fine scroll and vehicle sprites remain gated to State 21");
}

void test_unsigned_position_bounds(const Payload& payload)
{
    auto left = patterned_state();
    left.vehicle_position63 = 0;
    left.direction64 = 0xFF;
    std::uint8_t d016 = 0;
    update_drive_frame(payload, left, d016);
    check(left.vehicle_position63 == 0xFF && d016 == 6,
          "left helper preserves 6502 byte wrap and remains screen-bounded");

    auto right = patterned_state();
    right.vehicle_position63 = 0xFF;
    right.direction64 = 1;
    update_drive_frame(payload, right, d016);
    check(right.vehicle_position63 == 0 && d016 == 0,
          "right helper preserves 6502 byte wrap and remains screen-bounded");
}

void test_vehicle_y_table(const Payload& payload)
{
    constexpr std::uint8_t expected[] = {0x86, 0x87, 0x84, 0x94};
    for (std::uint8_t vehicle = 0; vehicle < 4; ++vehicle) {
        auto state = patterned_state();
        state.vehicle5c = vehicle;
        std::uint8_t d016 = 0;
        update_drive_frame(payload, state, d016);
        check(state.city.sprites.y[2] == expected[vehicle] &&
                  state.city.sprites.y[3] == expected[vehicle],
              "vehicle-specific drive sprite Y table");
    }
}

void test_beacon_color(const Payload& payload)
{
    constexpr std::uint8_t animation[] = {0, 1, 0x0F, 0x10, 0x1F, 0x20};
    constexpr std::uint8_t expected[] = {0x0D, 0x06, 0x01, 0x01, 0x06, 0x06};
    for (std::size_t i = 0; i < std::size(animation); ++i) {
        auto state = patterned_state();
        state.animation65 = animation[i];
        state.city.sprites.colors[2] = 0x0D;
        std::uint8_t d016 = 0;
        update_drive_frame(payload, state, d016);
        check(state.city.sprites.colors[2] == expected[i],
              "drive beacon folds animation phase into sprite 2 color");
    }

    auto state = patterned_state();
    state.city.state3a = 0x14;
    state.animation65 = 1;
    state.city.sprites.colors[2] = 0x0D;
    std::uint8_t d016 = 0xDB;
    update_drive_frame(payload, state, d016);
    check(state.city.sprites.colors[2] == 0x06 && d016 == 0xDB,
          "beacon update follows the State-21-only coordinate and scroll block");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_left_boundary(payload);
        test_right_boundary(payload);
        test_subcharacter_and_state_gate(payload);
        test_unsigned_position_bounds(payload);
        test_vehicle_y_table(payload);
        test_beacon_color(payload);
        std::cout << "native drive-frame tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
