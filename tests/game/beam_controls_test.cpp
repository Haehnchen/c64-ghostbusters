#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/beam_controls.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::BeamAudioAction;
using ghostbusters::game::BeamAudioEvent;
using ghostbusters::game::BeamControls;
using ghostbusters::game::BeamControlsInput;
using ghostbusters::game::BeamControlsRegisters;
using ghostbusters::game::BuildingControlsRegisters;
using ghostbusters::game::DriveControlsState;
using ghostbusters::game::NoticeScroller;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

DriveControlsState beam_state()
{
    DriveControlsState state;
    auto& city = state.city;
    city.state3a = 0x1B;
    city.backpack_charge3e = 0x99;
    city.backup_men3d = 3;
    city.fire_latch11 = 0x10;
    city.owned_mask6d = 0x02;
    for (std::size_t i = 0; i < 8; ++i) {
        city.sprites.x[i] = city.sprites.target_x[i] =
            static_cast<std::uint8_t>(0x30U + i * 8U);
        city.sprites.y[i] = city.sprites.target_y[i] = 0xB0;
        city.sprites.pointers[i] = 0x0E;
    }
    city.sprites.x[4] = city.sprites.target_x[4] = 0x68;
    city.sprites.y[4] = city.sprites.target_y[4] = 0x50;
    city.sprites.x[5] = city.sprites.target_x[5] = 0x50;
    city.sprites.y[5] = city.sprites.target_y[5] = 0xB8;
    city.sprites.x[6] = city.sprites.target_x[6] = 0x80;
    city.sprites.y[6] = city.sprites.target_y[6] = 0xB8;
    return state;
}

BuildingControlsRegisters building_registers(
    std::array<std::uint8_t, 2> animation = {0, 0},
    std::uint8_t direction = 0,
    std::array<std::uint8_t, 2> beam_direction = {0, 0xFF})
{
    return BuildingControlsRegisters(0xA1, 0xB2, animation, direction,
                                     beam_direction);
}

BeamControlsRegisters beam_registers(std::uint8_t phase = 0,
                                     std::uint8_t scratch = 0xA5,
                                     std::uint8_t post_failure = 0x5A)
{
    return BeamControlsRegisters(phase, scratch, post_failure);
}

std::array<std::uint8_t, 64> generated_sprite(const Payload& payload,
                                               std::uint8_t pointer)
{
    auto data = ghostbusters::game::shared_sprite_data(payload);
    data.resize(0x1100);
    auto reverse = [](std::uint8_t value) {
        return static_cast<std::uint8_t>(((value & 3U) << 6U) |
                                         ((value & 12U) << 2U) |
                                         ((value & 48U) >> 2U) |
                                         ((value & 192U) >> 6U));
    };
    std::uint8_t x = 0xFC;
    for (;;) {
        const auto i = static_cast<std::size_t>(x);
        data[0x0F02 + i] = reverse(data[0x0640 + i]);
        data[0x0F01 + i] = reverse(data[0x0641 + i]);
        data[0x0F00 + i] = reverse(data[0x0642 + i]);
        data[0x1002 + i] = reverse(data[0x0740 + i]);
        data[0x1001 + i] = reverse(data[0x0741 + i]);
        data[0x1000 + i] = reverse(data[0x0742 + i]);
        x = static_cast<std::uint8_t>(x - 3U);
        if (x == 0xFD) break;
        if ((x & 0x3F) == 0x3D) {
            --x;
            if (x == 0) break;
        }
    }
    std::array<std::uint8_t, 64> result{};
    std::copy_n(data.begin() + static_cast<std::size_t>(pointer) * 64U,
                result.size(), result.begin());
    return result;
}

void test_beam_update_and_generated_right_bitmap(const Payload& payload)
{
    auto state = beam_state();
    state.city.sprites.pointers[5] = 0x0F;
    BeamControls controls(payload, state, building_registers({1, 0}),
                          beam_registers(5));
    const auto result = controls.tick({0, 1, 0xFF});
    const auto& after = controls.state().city.sprites;
    check(result.wait_irqs == 0 && result.audio_events.empty() &&
              controls.state().city.state3a == 0x1B,
          "ordinary State 1B tick stays active and emits no audio");
    check(controls.registers().beam_phase79 == 0 &&
              controls.registers().beam_scratch7a == 0 &&
              after.y_expand_mask == 0x0F,
          "$9080 wraps phase six and updates $7A/$D017");
    check(after.x[0] == 0x48 && after.x[2] == 0x3F &&
              after.y[0] == 0x8F && after.y[2] == 0x67,
          "left-facing sprite 5 produces the exact two beam coordinates");
    check(after.pointers[0] == 0x3C && after.pointers[2] == 0x3C &&
              after.bitmap_data[0] == generated_sprite(payload, 0x3C) &&
              after.colors[0] ==
                  2,
          "right-side beam pointers resolve generated bitmap and IRQ-derived color");

    // These final right-facing frames contain packed table values above the
    // VIC color range. The IRQ exposes only their low nibble.
    for (const auto [phase, pointer, color] :
         std::array<std::array<std::uint8_t, 3>, 3>{{{2, 0x3F, 8},
                                                     {3, 0x40, 2},
                                                     {4, 0x41, 0}}}) {
        auto color_state = beam_state();
        BeamControls color_controls(payload, color_state,
                                    building_registers({1, 1}),
                                    beam_registers(phase));
        (void)color_controls.tick({0, 1, 0xFF});
        const auto& color_sprites = color_controls.state().city.sprites;
        check(color_sprites.pointers[0] == pointer &&
                  color_sprites.pointers[1] == pointer &&
                  color_sprites.colors[0] == color &&
                  color_sprites.colors[1] == color,
              "beam pointers $3F-$41 cache the low nibble written by $8F99");
    }
}

void test_target_distance_boundaries(const Payload& payload)
{
    auto at_twelve = beam_state();
    at_twelve.city.backpack_charge3e = 0;
    at_twelve.city.sprites.target_x[5] = 0x50;
    at_twelve.city.sprites.target_x[6] = 0x5C;
    at_twelve.city.sprites.x[5] = 0x50;
    at_twelve.city.sprites.x[6] = 0x5C;
    BeamControls moving(payload, at_twelve, building_registers(), beam_registers());
    (void)moving.tick({0, 1, 0xF7}); // right is permitted only for the left buster
    check(moving.state().city.sprites.target_x[5] == 0x51 &&
              moving.state().city.sprites.target_x[6] == 0x5C,
          "target distance 12 applies inward-only joystick masks");

    auto at_eleven = beam_state();
    at_eleven.city.backpack_charge3e = 0;
    at_eleven.city.sprites.target_x[5] = 0x50;
    at_eleven.city.sprites.target_x[6] = 0x5B;
    at_eleven.city.sprites.x[5] = 0x50;
    at_eleven.city.sprites.x[6] = 0x5B;
    BeamControls still(payload, at_eleven, building_registers(), beam_registers());
    (void)still.tick({0, 1, 0xF7});
    check(still.state().city.sprites.target_x[5] == 0x50 &&
              still.state().city.sprites.target_x[6] == 0x5B,
          "target distance 11 suppresses both control updates");
}

void test_normal_finishes_and_bcd(const Payload& payload)
{
    auto fire_state = beam_state();
    fire_state.city.key17 = 0x20;
    fire_state.city.sprites.pointers[5] = 0x17;
    fire_state.city.sprites.pointers[6] = 0x16;
    BeamControls fire(payload, fire_state, building_registers(), beam_registers());
    const auto fire_result = fire.tick({0, 1, 0xEF});
    check(fire.state().city.state3a == 0x1C && fire.state().city.key17 == 0 &&
              fire.state().city.sprites.pointers[5] == 0x0F &&
              fire.state().city.sprites.pointers[6] == 0x0E &&
              fire.state().city.sprites.colors[5] ==
                  7 &&
              fire.state().city.sprites.colors[6] ==
                  7,
          "new fire press restores pointer-derived visuals and enters State $1C");
    check(fire_result.audio_events == std::vector<BeamAudioEvent>{
              {BeamAudioAction::voice3_stop, 0},
              {BeamAudioAction::voice3_start, 3}},
          "normal finish preserves ordered stop then effect-3 start even if busy");

    for (const auto [before, after] :
         std::array<std::array<std::uint8_t, 2>, 4>{{{0x99, 0x98},
                                                     {0x10, 0x09},
                                                     {0x01, 0x00},
                                                     {0x00, 0x00}}}) {
        auto state = beam_state();
        state.city.backpack_charge3e = before;
        state.city.key17 = 0x42;
        BeamControls drain(payload, state, building_registers(), beam_registers());
        const auto result = drain.tick({0, 0, 0xFF});
        check(drain.state().city.backpack_charge3e == after,
              "packed-BCD drain matches 6502 decimal subtraction");
        check((after == 0) == (drain.state().city.state3a == 0x1C) &&
                  (after != 0 || drain.state().city.key17 == 0) &&
                  (after != 0 || result.audio_events.size() == 2),
              "zero BCD charge clears key, queues shutdown and ordered audio");
    }
}

void test_crossed_stream_irq_wait_and_busy_notice(const Payload& payload)
{
    auto state = beam_state();
    state.city.sprites.target_x[5] = 0x50;
    state.city.sprites.target_x[6] = 0x70;
    state.city.sprites.x[5] = 0x60;
    state.city.sprites.x[6] = 0x61;
    state.city.sprites.pointers[5] = 0x16;
    state.city.sprites.pointers[6] = 0x17;
    NoticeScroller::Snapshot busy{};
    busy.position4b = 7;
    busy.length4a = 9;
    busy.buffer[0] = 0x2A;
    state.city.notices = NoticeScroller(busy);
    BeamControls controls(payload, state, building_registers(),
                          beam_registers(0, 0x55, 0x99));

    const auto crossed = controls.tick({1, 9, 0xEF});
    check(crossed.wait_irqs == 64 && crossed.audio_events.empty() &&
              controls.pending_irqs() == 64 && controls.state().city.state3a == 0x1B,
          "crossed beams expose a 64-IRQ scheduler wait without polling fire");
    const auto frozen_latch = controls.state().city.fire_latch11;
    for (int i = 0; i < 63; ++i) {
        const auto resumed = controls.resume_irq();
        check(resumed.audio_events.empty(),
              "first 63 special IRQs do not clean up or stop audio");
    }
    check(controls.pending_irqs() == 1 && controls.state().city.state3a == 0x1B &&
              controls.state().city.fire_latch11 == frozen_latch,
          "special IRQ loop leaves handler input and state frozen");
    const auto final = controls.resume_irq();
    const auto& city = controls.state().city;
    check(final.wait_irqs == 0 && final.audio_events == std::vector<BeamAudioEvent>{
              {BeamAudioAction::voice3_stop, 0}},
          "64th special IRQ performs cleanup and emits only voice-3 stop");
    check(city.state3a == 0x1D && city.backpack_charge3e == 0 &&
              city.backup_men3d == 1 && city.sprites.target_x[4] == 0 &&
              city.sprites.target_y[4] == 0 &&
              controls.registers().post_failure_ea7a == 0,
          "crossed-stream cleanup enters State $1D and updates all persistent bytes");
    check(city.sprites.pointers[0] == 0 && city.sprites.pointers[1] == 0 &&
              city.sprites.pointers[2] == 0 && city.sprites.pointers[3] == 0 &&
              city.sprites.pointers[5] == 0x18 && city.sprites.pointers[6] == 0x18 &&
              city.sprites.colors[5] ==
                  7 &&
              city.sprites.colors[6] ==
                  7,
          "crossed-stream cleanup refreshes pointer-$18 visuals for both busters");
    check(city.notices.position4b() == 7 && city.notices.length4a() == 9 &&
              city.notices.buffer()[0] == 0x2A,
          "busy notice buffer discards notice 8 without affecting failure cleanup");
    check(controls.registers().beam_phase79 == 5,
          "one handler plus 64 IRQ updates advance the six-frame phase 65 times");
}

void test_strict_crossing_limit_and_stable_handoffs(const Payload& payload)
{
    auto state = beam_state();
    state.city.sprites.target_x[5] = 0x40;
    state.city.sprites.target_x[6] = 0x80;
    state.city.sprites.x[5] = 0x50;
    state.city.sprites.x[6] = 0x78; // $2A apart after the one-pixel approach
    state.city.sprites.pointers[5] = 0x16;
    state.city.sprites.pointers[6] = 0x17;
    BeamControls controls(payload, state, building_registers(), beam_registers());
    const auto boundary = controls.tick({0, 1, 0xFF});
    check(boundary.wait_irqs == 0 && controls.state().city.state3a == 0x1B,
          "current beam distance equal to the computed limit does not cross");

    auto clamped_state = beam_state();
    clamped_state.city.sprites.target_x[5] = 0x12;
    clamped_state.city.sprites.target_x[6] = 0x00;
    clamped_state.city.sprites.x[5] = 0x40;
    clamped_state.city.sprites.x[6] = 0x3E;
    clamped_state.city.sprites.pointers[5] = 0x17;
    clamped_state.city.sprites.pointers[6] = 0x16;
    BeamControls clamped(payload, clamped_state, building_registers(),
                         beam_registers());
    const auto clamped_crossing = clamped.tick({0, 1, 0xFF});
    check(clamped_crossing.wait_irqs == 64 &&
              clamped.state().city.sprites.target_x[5] == 0x20 &&
              clamped.state().city.sprites.target_x[6] == 0x20,
          "crossing keeps the pre-clamp reversed-target ordering from $85B8");

    controls.state().city.state3a = 0x1C;
    const auto phase = controls.registers().beam_phase79;
    const auto stable_1c = controls.tick({0xFF, 0, 0});
    check(stable_1c.wait_irqs == 0 && stable_1c.audio_events.empty() &&
              controls.state().city.state3a == 0x1C &&
              controls.registers().beam_phase79 == phase,
          "State $1C is a stable handoff after normal beam shutdown");
    controls.state().city.state3a = 0x1D;
    const auto stable_1d = controls.tick({0xFF, 0, 0});
    check(stable_1d.wait_irqs == 0 && stable_1d.audio_events.empty() &&
              controls.state().city.state3a == 0x1D &&
              controls.registers().beam_phase79 == phase,
          "State $1D is a stable handoff after crossed-stream cleanup");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_beam_update_and_generated_right_bitmap(payload);
        test_target_distance_boundaries(payload);
        test_normal_finishes_and_bcd(payload);
        test_crossed_stream_irq_wait_and_busy_notice(payload);
        test_strict_crossing_limit_and_stable_handoffs(payload);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) return 1;
    std::cout << "beam controls tests passed\n";
    return 0;
}
