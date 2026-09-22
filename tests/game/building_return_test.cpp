#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/building_return.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::BuildingControlsRegisters;
using ghostbusters::game::BuildingReturn;
using ghostbusters::game::BuildingReturnInput;
using ghostbusters::game::DriveControlsState;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

DriveControlsState return_state(std::uint8_t handler)
{
    DriveControlsState state;
    state.city.state3a = handler;
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        state.city.sprites.pointers[sprite] = 0x0E;
        state.city.sprites.x[sprite] = state.city.sprites.target_x[sprite] =
            static_cast<std::uint8_t>(0x40 + sprite);
        state.city.sprites.y[sprite] = state.city.sprites.target_y[sprite] =
            static_cast<std::uint8_t>(0xB0 + sprite);
    }
    return state;
}

BuildingControlsRegisters registers(std::array<std::uint8_t, 2> animation = {0, 0})
{
    return BuildingControlsRegisters(0xA1, 0xB2, animation, 3, {0x44, 0x55});
}

void test_state20_arrival_and_targets(const Payload& payload)
{
    auto state = return_state(0x20);
    auto& sprites = state.city.sprites;
    sprites.x[5] = 0xA7;
    sprites.target_x[5] = 0xA8;
    sprites.y[5] = 0xC6;
    sprites.target_y[5] = 0xC7;
    BuildingReturn result(payload, state, registers());
    result.tick({0});

    check(result.state().city.state3a == 0x21,
          "State 32 advances to State 33 after installing the offscreen targets");
    check(result.state().city.sprites.x[5] == 0xA8 &&
              result.state().city.sprites.y[5] == 0xC7,
          "$9AE3 reaches sprite 5 by one pixel per axis before $9A01");
    check(result.state().city.sprites.target_x[5] == 0xA8 &&
              result.state().city.sprites.target_x[6] == 0xA8 &&
              result.state().city.sprites.target_y[5] == 0xC7 &&
              result.state().city.sprites.target_y[6] == 0xC7,
          "$8875 installs A8/C7 for both returning busters");
    check(result.state().city.sprites.pointers[5] == 0x10,
          "even-frame animation advances a moving sprite pointer after facing setup");
    check(result.state().city.sprites.pointers[6] == 0x0E,
          "a settled companion keeps the State-32 settled pointer base");
}

void test_state20_waits_for_y_arrival(const Payload& payload)
{
    auto state = return_state(0x20);
    auto& sprites = state.city.sprites;
    sprites.x[5] = sprites.target_x[5] = 0xA8;
    sprites.y[5] = 0xC5;
    sprites.target_y[5] = 0xC7;
    BuildingReturn result(payload, state, registers());
    result.tick({1});
    check(result.state().city.sprites.target_x[5] == 0xA8 &&
              result.state().city.sprites.target_y[5] == 0xC7 &&
              result.state().city.sprites.target_x[6] != 0xA8,
          "$9A01 requires both coordinates before State 32 changes targets");
}

void test_state21_handoff_and_trap_follow(const Payload& payload)
{
    auto state = return_state(0x21);
    auto& sprites = state.city.sprites;
    sprites.x[5] = sprites.y[5] = 0xA7;
    sprites.target_x[5] = sprites.target_y[5] = 0xA8;
    sprites.x[6] = sprites.y[6] = 0xA7;
    sprites.target_x[6] = sprites.target_y[6] = 0xA8;
    BuildingReturn result(payload, state, registers());
    result.tick({0});

    check(result.state().city.state3a == 0x11,
          "$8896 hands the completed return to State 17");
    check(result.state().city.sprites.x[7] == 0xAE &&
              result.state().city.sprites.y[7] == 0xA1 &&
              result.state().city.sprites.target_x[7] == 0xAE &&
              result.state().city.sprites.target_y[7] == 0xA1,
          "$9A16 follows sprite 5 six pixels sideways and seven rows above");
    check(result.state().city.sprites.pointers[5] == 0x10 &&
              result.state().city.sprites.pointers[6] == 0x10,
          "State 33 keeps moving pointers through the final arrival tick");
}

void test_state_validation(const Payload& payload)
{
    auto state = return_state(0x1F);
    try {
        BuildingReturn invalid(payload, state, registers());
        (void)invalid;
        check(false, "constructor rejects handlers outside States 32/33");
    } catch (const std::invalid_argument&) {
        check(true, "constructor rejects handlers outside States 32/33");
    }
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_state20_arrival_and_targets(payload);
        test_state20_waits_for_y_arrival(payload);
        test_state21_handoff_and_trap_follow(payload);
        test_state_validation(payload);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
