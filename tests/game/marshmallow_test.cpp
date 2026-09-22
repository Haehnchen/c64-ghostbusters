#include "assets/payload.hpp"
#include "assets/city_data.hpp"
#include "assets/payload.hpp"
#include "game/marshmallow.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::assets::MarshmallowData;
using ghostbusters::game::CityControlsState;
using ghostbusters::game::Marshmallow;
using ghostbusters::game::MarshmallowRegisters;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Function>
void expect_out_of_range(Function&& function, const char* message)
{
    try {
        function();
        check(false, message);
    } catch (const std::out_of_range&) {
    } catch (...) {
        check(false, message);
    }
}

void test_semantic_map_data(const Payload& payload)
{
    const MarshmallowData data(payload);
    for (std::uint8_t row = 0; row < 26; ++row) {
        for (unsigned column = 0; column < 256; ++column) {
            const auto screen_offset = 124U + static_cast<unsigned>(row) * 40U + column;
            if (screen_offset < 0x400U) {
                const auto cell = data.trail_cell(
                    row, static_cast<std::uint8_t>(column));
                check(cell.screen_offset == screen_offset &&
                          cell.linear_low == (screen_offset & 0xFFU),
                      "trail row matches every native screen cell");
            } else {
                expect_out_of_range(
                    [&] {
                        (void)data.trail_cell(
                            row, static_cast<std::uint8_t>(column));
                    },
                    "trail rejects every source-table cell outside the screen");
            }
        }
    }
    const auto trail = data.trail_cell(15, 10);
    check(trail.screen_offset == 0x2DE && trail.linear_low == 0xDE,
          "trail cell exposes bounded native offsets");
    const auto clipped = data.city_map_quadrant(0, 0, 0);
    check(clipped.screen_offset == -34 && clipped.cells.size() == 5,
          "city map retains its signed above-screen first destination");
    const auto visible = data.city_map_quadrant(0, 0, 1);
    check(visible.screen_offset == 6 && visible.cells.size() == 5,
          "city map row stride moves the next row into the native screen");
    expect_out_of_range([&] { (void)data.trail_cell(26, 0); },
                        "trail rejects row 26");
    expect_out_of_range([&] { (void)data.trail_cell(22, 20); },
                        "trail rejects a column beyond the native screen");
    expect_out_of_range([&] { (void)data.city_map_quadrant(30, 0, 0); },
                        "city map rejects building 30");
    expect_out_of_range([&] { (void)data.city_map_quadrant(0, 8, 0); },
                        "city map rejects selector eight");
    expect_out_of_range([&] { (void)data.city_map_quadrant(0, 0, 4); },
                        "city map rejects row four");
}

CityControlsState marshmallow_state(std::uint8_t handler)
{
    CityControlsState state;
    state.state3a = handler;
    state.key17 = 0x55;
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        state.sprites.x[sprite] = state.sprites.target_x[sprite] =
            static_cast<std::uint8_t>(0x30 + sprite);
        state.sprites.y[sprite] = state.sprites.target_y[sprite] =
            static_cast<std::uint8_t>(0x50 + sprite);
        state.sprites.pointers[sprite] = static_cast<std::uint8_t>(sprite + 1);
        state.sprite_control_c0[sprite] = static_cast<std::uint8_t>(0x10 + sprite);
        state.shadow_x_ea46[sprite] = static_cast<std::uint8_t>(0x60 + sprite);
        state.shadow_y_ea47[sprite] = static_cast<std::uint8_t>(0x70 + sprite);
        state.shadow_target_x_ea56[sprite] = static_cast<std::uint8_t>(0x80 + sprite);
        state.shadow_target_y_ea57[sprite] = static_cast<std::uint8_t>(0x90 + sprite);
    }
    state.sprites.x[0] = 0x47;
    state.sprites.y[0] = 0xBA;
    return state;
}

void test_state36_tail_bait_and_partial_arrivals(const Payload& payload)
{
    auto state = marshmallow_state(0x24);
    state.key17 = 0x42;
    state.bait69 = 1;
    state.sprites.x[4] = 0x10;
    state.sprites.target_x[4] = 0x12;
    Marshmallow sequence(payload, state, {0xA1, 0xB2, 0xC3});
    sequence.tick();

    const auto& result = sequence.state();
    check(result.state3a == 0x24 && result.sprites.x[4] == 0x11,
          "State 36 moves only one pixel and waits for the remaining roamer");
    check(result.sprites.pointers[5] == 0x2C && result.sprites.pointers[6] == 0x2D &&
              result.sprites.pointers[7] == 0x2E && result.sprites.pointers[4] == 5,
          "State 36 assigns arrival pointers independently");
    check(result.key17 == 0 && result.bait69 == 0 && result.bait_active68 == 1,
          "the $7F29 tail consumes B and activates available bait");
    check(result.route_length66 == 1 && result.last_map_cell1f == 0xDE &&
              result.characters.screen[0x2DE] == 0x41 &&
              result.characters.screen[0x2DF] == 0x42 &&
              result.characters.colors[0x2DE] == 3 &&
              result.characters.colors[0x2DF] == 3,
          "State 36 retains city trail and bait-cell side effects");
    check(sequence.trail_color_row_offset() == 0x2D4,
          "State 36 reports the native color row changed by the trail");
    check(sequence.registers().scratch23 == 1 &&
              sequence.registers().scratch24 == 2,
          "native trail drawing does not synthesize source-machine addresses");
    check(result.sprites.target_x[4] == 0x41 && result.sprites.target_x[5] == 0x41 &&
              result.sprites.target_x[6] == 0x4D && result.sprites.target_x[7] == 0x4D &&
              result.sprites.target_y[4] == 0xB0 && result.sprites.target_y[6] == 0xB0 &&
              result.sprites.target_y[5] == 0xC5 && result.sprites.target_y[7] == 0xC5,
          "bait retargets all marshmallow sprites around the player");
}

void test_state36_arrival_branches(const Payload& payload)
{
    auto attack = marshmallow_state(0x24);
    attack.key17 = 0x42;
    Marshmallow unbaited(payload, attack, {9, 8, 7});
    unbaited.tick();
    check(unbaited.state().state3a == 0x25 && unbaited.state().countdown7c == 0xFF &&
              unbaited.state().key17 == 0 && unbaited.registers().scratch23 == 0,
          "unbaited arrival starts State 37 through the increment helper");
    check(unbaited.state().sprites.pointers[4] == 0x2B &&
              unbaited.state().sprites.pointers[5] == 0x2C &&
              unbaited.state().sprites.pointers[6] == 0x2D &&
              unbaited.state().sprites.pointers[7] == 0x2E,
          "all four State-36 arrival pointers match $A905-$A908");

    auto reward = marshmallow_state(0x24);
    reward.bait_active68 = 1;
    Marshmallow baited(payload, reward, {9, 8, 7});
    baited.tick();
    check(baited.state().state3a == 0x26 && baited.state().countdown7c == 0xFF &&
              baited.state().bait_active68 == 0 && baited.state().key17 == 0x55,
          "bait skips State 37, clears $68 and preserves key $17");
}

void test_state37_animation_and_map_quadrants(const Payload& payload)
{
    auto state = marshmallow_state(0x25);
    state.countdown7c = 0xE0;
    state.pending_alert80 = 5;
    state.map_types_ea28[5] = 3;
    state.sprites.target_y[4] = 0x50;
    Marshmallow sequence(payload, state, {0, 0, 0x77});
    sequence.tick();

    const auto& result = sequence.state();
    check(result.sprites.pointers[5] == 0x2C && result.sprites.pointers[7] == 0x2E &&
              result.sprites.y[4] == 0x53 && result.sprites.y[6] == 0x53 &&
              result.sprites.y[5] == 0x68 && result.sprites.y[7] == 0x68,
          "State 37 derives pointers and vertical attack motion from countdown $E0");
    check(result.map_types_ea28[5] == 0 && sequence.registers().scratch23 == 5 &&
              sequence.registers().scratch24 == 0 && sequence.registers().scratch25 == 0xFF,
          "countdown $E0 destroys quadrant zero and retains $95EB scratch results");

    const ghostbusters::assets::CityData data(payload);
    const auto destination = data.map_destination(5);
    const auto cells = data.map_row(0, 0);
    bool quadrant_matches = true;
    for (std::size_t column = 0; column < 5; ++column) {
        quadrant_matches &= result.characters.screen[
                                static_cast<std::size_t>(destination) + column] ==
                            cells[column];
    }
    check(quadrant_matches, "$95EB redraws the cleared map type's five-byte quadrant");

    auto phase_state = marshmallow_state(0x25);
    phase_state.countdown7c = 0xD8;
    phase_state.sprites.target_y[4] = 0x50;
    Marshmallow phase(payload, phase_state, {0, 0, 0x77});
    phase.tick();
    check(phase.state().sprites.pointers[5] == 0x31 &&
              phase.state().sprites.pointers[7] == 0x2E &&
              phase.state().sprites.y[4] == 0x51 &&
              phase.registers().scratch23 == 4 && phase.registers().scratch25 == 0x77,
          "non-boundary State 37 animates without redrawing a map quadrant");
}

void test_state37_failure_cost_and_restore(const Payload& payload)
{
    auto state = marshmallow_state(0x25);
    state.countdown7c = 0;
    state.balance57 = {0, 0x30, 0};
    Marshmallow sequence(payload, state, {0xA1, 0xB2, 0xC3});
    sequence.tick();

    const auto& result = sequence.state();
    check(result.state3a == 0x27 && result.balance57 == ghostbusters::game::AccountBalanceBytes{} &&
              result.notices.position4b() == 1,
          "State 37 queues notice 5 and clamps an unaffordable $4000 loss to zero");
    bool restored = true;
    for (std::size_t sprite = 4; sprite < 8; ++sprite) {
        restored &= result.sprites.target_x[sprite] == result.shadow_x_ea46[sprite];
        restored &= result.sprites.target_y[sprite] == result.shadow_y_ea47[sprite];
        restored &= result.sprites.pointers[sprite] == result.sprite_control_c0[sprite];
    }
    check(restored, "$983D restores saved city positions and original sprite pointers");
    check(sequence.registers().scratch23 == 0 && sequence.registers().scratch24 == 0x40 &&
              sequence.registers().scratch25 == 0 && result.key17 == 0x55,
          "the direct failure handoff preserves key $17 and the subtraction operands");
}

void test_state38_reward_and_overflow(const Payload& payload)
{
    auto waiting = marshmallow_state(0x26);
    waiting.countdown7c = 1;
    Marshmallow wait(payload, waiting, {1, 2, 3});
    wait.tick();
    check(wait.state().state3a == 0x26 && wait.state().sprites.pointers[4] == 0x2F &&
              wait.state().sprites.pointers[6] == 0x30 &&
              wait.state().sprites.pointers[5] == 6 && wait.state().sprites.pointers[7] == 8,
          "State 38 changes only sprites 4 and 6 while its countdown remains active");

    auto reward = marshmallow_state(0x26);
    reward.countdown7c = 0;
    reward.balance57 = {0, 0x80, 0};
    Marshmallow paid(payload, reward, {1, 2, 3});
    paid.tick();
    check(paid.state().state3a == 0x27 &&
              paid.state().balance57 == ghostbusters::game::AccountBalanceBytes{1, 0, 0} &&
              paid.state().key17 == 0 && paid.state().notices.position4b() == 1,
          "State 38 adds $2000, queues notice 6 and advances through $8D86");

    auto overflow = marshmallow_state(0x26);
    overflow.countdown7c = 0;
    overflow.balance57 = {0x99, 0x90, 0x45};
    Marshmallow capped(payload, overflow, {1, 2, 3});
    capped.tick();
    check(capped.state().balance57 ==
              ghostbusters::game::AccountBalanceBytes{0x99, 0x99, 0x45},
          "$9664 overflow caps high/middle BCD bytes while preserving byte $59");
}

void test_state39_returns_to_original_routes(const Payload& payload)
{
    auto state = marshmallow_state(0x27);
    for (std::size_t sprite = 4; sprite < 8; ++sprite) {
        state.sprites.target_x[sprite] = static_cast<std::uint8_t>(0x40 + sprite);
        state.sprites.x[sprite] = static_cast<std::uint8_t>(state.sprites.target_x[sprite] - 1);
        state.sprites.target_y[sprite] = state.sprites.y[sprite];
    }
    Marshmallow sequence(payload, state, {9, 8, 7});
    sequence.tick();
    bool route_targets = sequence.state().state3a == 0x12;
    for (std::size_t sprite = 4; sprite < 8; ++sprite) {
        route_targets &= sequence.state().sprites.target_x[sprite] ==
                         sequence.state().shadow_target_x_ea56[sprite];
        route_targets &= sequence.state().sprites.target_y[sprite] ==
                         sequence.state().shadow_target_y_ea57[sprite];
    }
    check(route_targets,
          "State 39 reaches saved positions, restores original routes and selects State 18");
    check(sequence.registers().scratch23 == 1 && sequence.registers().scratch24 == 2 &&
              sequence.registers().scratch25 == 7 && sequence.state().key17 == 0x55,
          "State 39 exposes movement-helper scratch and preserves key $17");

    auto not_ready = marshmallow_state(0x27);
    not_ready.sprites.target_x[4] = static_cast<std::uint8_t>(not_ready.sprites.x[4] + 2);
    Marshmallow waiting(payload, not_ready, {0, 0, 0});
    waiting.tick();
    check(waiting.state().state3a == 0x27,
          "State 39 waits when any sprite 4-7 coordinate remains unequal");
}

void test_validation_and_audio(const Payload& payload)
{
    auto state = marshmallow_state(0x23);
    try {
        Marshmallow invalid(payload, state, {0, 0, 0});
        (void)invalid;
        check(false, "constructor rejects handlers outside States 36-39");
    } catch (const std::invalid_argument&) {
        check(true, "constructor rejects handlers outside States 36-39");
    }
    state.state3a = 0x24;
    Marshmallow silent(payload, state, {0, 0, 0});
    silent.tick();
    check(silent.audio_events().empty(), "States 36-39 emit no direct audio events");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_semantic_map_data(payload);
        test_state36_tail_bait_and_partial_arrivals(payload);
        test_state36_arrival_branches(payload);
        test_state37_animation_and_map_quadrants(payload);
        test_state37_failure_cost_and_restore(payload);
        test_state38_reward_and_overflow(payload);
        test_state39_returns_to_original_routes(payload);
        test_validation_and_audio(payload);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
    if (failures == 0) std::cout << "marshmallow tests passed\n";
    return failures == 0 ? 0 : 1;
}
