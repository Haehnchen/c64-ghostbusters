#include "assets/payload.hpp"
#include "game/zuul.hpp"
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
using ghostbusters::game::BuildingControlsRegisters;
using ghostbusters::game::DriveControlsState;
using ghostbusters::game::Zuul;
using ghostbusters::game::ZuulAudioAction;
using ghostbusters::game::ZuulAudioEvent;
using ghostbusters::game::ZuulInput;
using ghostbusters::game::ZuulRegisters;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

DriveControlsState state(std::uint8_t handler)
{
    DriveControlsState value;
    value.city.state3a = handler;
    value.city.key17 = 0x72;
    value.city.countdown7c = 0x44;
    value.city.backup_men3d = 7;
    value.city.sprites.x.fill(0x11);
    value.city.sprites.y.fill(0x22);
    value.city.sprites.target_x.fill(0x33);
    value.city.sprites.target_y.fill(0x44);
    value.city.sprites.pointers.fill(0x12);
    value.city.sprites.colors.fill(0x0D);
    return value;
}

BuildingControlsRegisters building()
{
    return {0x91, 0x92, {0, 1}, 0x95, {0x96, 0x97}};
}

ZuulRegisters registers(std::uint8_t gate_phase = 1,
                        std::uint8_t gatekeeper = 2,
                        std::uint8_t keymaster = 2,
                        std::uint8_t beam_phase = 0)
{
    return {0x81, 0x82, 0x83, gate_phase, gatekeeper, keymaster,
            beam_phase, 0x88};
}

void test_state40_initializes_rooftop(const Payload& payload)
{
    Zuul zuul(payload, state(0x28), building(), registers());
    const auto result = zuul.tick();
    const auto& city = zuul.state().city;
    const auto& sprites = city.sprites;
    check(result.audio_events.empty() && city.state3a == 0x29 && city.key17 == 0,
          "State 40 advances through $8D86 without audio");
    check(city.countdown7c == 0 && city.backup_men3d == 3 &&
              zuul.registers().gatekeeper_ea78 == 2 &&
              zuul.registers().keymaster_ea79 == 2 &&
              zuul.registers().gate_phase_ea77 == 1,
          "State 40 initializes countdown, crew, and EA77-EA79");
    check(sprites.pointers == std::array<std::uint8_t, 8>{
              0x2B,0x2C,0x2D,0x2E,0,0,0,0} &&
              sprites.x[5] == 0xA8 && sprites.y[5] == 0xC7 &&
              sprites.target_x[5] == 0x60 && sprites.target_y[5] == 0xC0,
          "State 40 installs exact rooftop pointers and gate targets");
    check(sprites.multicolor_mask == 0xF0 && sprites.x_expand_mask == 0x0F &&
              sprites.y_expand_mask == 0x0F,
          "State 40 writes the three VIC sprite masks");
    check(sprites.colors[0] == 1 &&
              sprites.bitmap_data[0][0] ==
                  ghostbusters::game::shared_sprite_data(payload)[0x2B * 64],
          "State 40 resolves pointer color and bitmap data");
}

void test_state41_collision_speech_and_resume(const Payload& payload)
{
    auto active = state(0x29);
    active.city.countdown7c = 0;
    auto& sprites = active.city.sprites;
    sprites.x.fill(0);
    sprites.y.fill(0);
    sprites.target_x.fill(0);
    sprites.target_y.fill(0);
    sprites.x[5] = 0x60;
    sprites.y[5] = 0xC0;
    sprites.target_x[5] = 0x50;
    sprites.target_y[5] = 0xB0;
    sprites.x[0] = 0x50;
    sprites.y[0] = 0xB0;
    Zuul zuul(payload, active, building(), registers(0));
    const auto request = zuul.tick({4, 0, 0xFF, 0x21});
    check(request.blocking_request && request.collision_read &&
              !request.collision_clear_read && request.audio_events ==
                  std::vector<ZuulAudioEvent>{{ZuulAudioAction::speech_start, 3}},
          "$8A9C consumes collision and suspends before the $8B77 tail read");
    check(zuul.state().city.sprites.x[0] == 0x50 &&
              zuul.state().city.sprites.y[0] == 0xB0,
          "collision speech preserves the gate sprite before its animation tail");
    check(zuul.state().city.sprites.target_x[5] == 0x30 &&
              zuul.state().city.sprites.target_y[5] == 0xE5 &&
              zuul.state().city.characters.screen[0x3FD] == 0x18 &&
              zuul.state().city.backup_men3d == 6 &&
              zuul.registers().keymaster_ea79 == 1 &&
              zuul.state().city.countdown7c == 0,
          "collision mutations precede speech while its continuation remains pending");
    check(zuul.tick().blocking_request && zuul.state().city.countdown7c == 0,
          "blocked speech does not pretend another handler or IRQ tick occurred");
    const auto resumed = zuul.resume_speech();
    check(!resumed.blocking_request && resumed.collision_clear_read &&
              zuul.state().city.countdown7c == 0x2F &&
              zuul.registers().gate_phase_ea77 == 0xFF,
          "command 3 resumes after JSR, then consumes $8B77 and animates the gate");
}

void test_state41_gates_and_unconditional_latch_read(const Payload& payload)
{
    auto active = state(0x29);
    active.city.countdown7c = 1;
    Zuul animated(payload, active, building(), registers(1));
    const auto tail = animated.tick({3, 0, 0xFF, 0xFF});
    check(!tail.collision_read && tail.collision_clear_read &&
              animated.state().city.sprites.y[0] == 0x6C,
          "nonzero countdown skips $8A9C but performs discarded $8B77 read");

    auto doorway_state = state(0x29);
    doorway_state.city.countdown7c = 0;
    doorway_state.city.sprites.x[5] = 0x60;
    doorway_state.city.sprites.y[5] = 0xC0;
    doorway_state.city.sprites.target_x[5] = 0x5C;
    doorway_state.city.sprites.target_y[5] = 0xA9;
    Zuul doorway(payload, doorway_state, building(), registers(0));
    const auto doorway_tick = doorway.tick({1, 1, 0xFF, 0xFF});
    check(!doorway_tick.collision_read && doorway_tick.collision_clear_read &&
              doorway.registers().gatekeeper_ea78 == 1 &&
              doorway.registers().gate_phase_ea77 == 1 &&
              doorway.state().city.countdown7c == 0x3F &&
              doorway.state().city.backup_men3d == 6,
          "doorway interval resolves before the lower Y clamp without reading $8A9C");

    auto gate_done = state(0x29);
    gate_done.city.countdown7c = 0;
    Zuul state42(payload, gate_done, building(), registers(1, 0, 2));
    const auto next = state42.tick();
    check(next.audio_events.empty() && !next.collision_clear_read &&
              state42.raw_state() == 0x2A && state42.state().city.countdown7c == 0xCF &&
              state42.state().city.key17 == 0,
          "EA78 zero resets sprites and advances directly to State 42");

    auto failed = state(0x29);
    failed.city.countdown7c = 0;
    Zuul state46(payload, failed, building(), registers(1, 2, 0));
    (void)state46.tick();
    check(state46.raw_state() == 0x2E && state46.state().city.key17 == 0x72,
          "EA79 zero hands raw State 46 to the following ending package");
}

void test_state42_and_state43_scene_work(const Payload& payload)
{
    auto clearing = state(0x2A);
    clearing.city.countdown7c = 1;
    std::fill(clearing.city.characters.screen.begin() + 0x370,
              clearing.city.characters.screen.begin() + 0x398, 0xAA);
    Zuul row(payload, clearing, building(), registers());
    (void)row.tick();
    check(std::all_of(row.state().city.characters.screen.begin() + 0x370,
                      row.state().city.characters.screen.begin() + 0x398,
                      [](auto byte) { return byte == 0; }) && row.raw_state() == 0x2A,
          "State 42 clears exactly the bottom forty screen bytes while waiting");
    row.state().city.countdown7c = 0;
    (void)row.tick();
    check(row.raw_state() == 0x2B && row.state().city.countdown7c == 0xFF,
          "State 42 advances with the original zero-to-FF countdown write");

    auto climb = state(0x2B);
    climb.city.countdown7c = 0;
    for (std::size_t i = 0; i < 21 * 40; ++i) {
        climb.city.characters.screen[i] = static_cast<std::uint8_t>(i);
        climb.city.characters.colors[i] = static_cast<std::uint8_t>(i >> 3U);
    }
    const auto old_first = climb.city.characters.screen[0];
    Zuul setup(payload, climb, building(), registers());
    (void)setup.tick();
    const auto& after = setup.state().city;
    check(after.state3a == 0x2C && after.countdown7c == 0xFF &&
              after.characters.screen[40] == old_first,
          "State 43 runs the full screen/color row shift before advancing");
    check(after.characters.screen[0] == 0x4C &&
              after.characters.colors[0] == 0x0E &&
              setup.climb_color_offset() == 0,
          "$3300 countdown-zero top row uses exact static screen/color sources");
    check(setup.registers().scratch23 == 0x81 &&
              setup.registers().scratch24 == 0x82,
          "native climb drawing does not synthesize source-machine addresses");
    check(after.sprites.x[5] == 0x5E && after.sprites.x[6] == 0x84 &&
              after.sprites.y[5] == 0xA5 && after.sprites.y[6] == 0xA5 &&
              after.sprites.pointers == std::array<std::uint8_t, 8>{
                  0x19,0x19,0x19,0x19,0,0x16,0x17,0} &&
              setup.building_registers().animation77 ==
                  std::array<std::uint8_t, 2>{0,1},
          "State 43 prepares exact climber coordinates, pointers and directions");

    auto generated = state(0x2B);
    generated.city.countdown7c = 0x17;
    Zuul generated_row(payload, generated, building(), registers());
    (void)generated_row.tick();
    check(generated_row.state().city.countdown7c == 0x16 &&
              generated_row.state().city.characters.screen[39] ==
                  0x4C &&
              generated_row.state().city.characters.colors[39] ==
                  0x0E,
          "$3300 phase three decrements countdown before drawing generated top row");
}

void test_state44_speech_reward_and_raw_handoff(const Payload& payload)
{
    auto speech_state = state(0x2C);
    speech_state.city.countdown7c = 0x10;
    speech_state.city.sprites.x[5] = 0x5E;
    speech_state.city.sprites.x[6] = 0x84;
    speech_state.city.sprites.y[5] = speech_state.city.sprites.y[6] = 0x80;
    auto b = building();
    b.animation77 = {0, 1};
    Zuul speech(payload, speech_state, b, registers(1, 2, 2, 5));
    const auto request = speech.tick();
    check(request.blocking_request && request.audio_events ==
              std::vector<ZuulAudioEvent>{{ZuulAudioAction::speech_start, 4}} &&
              speech.raw_state() == 0x2C,
          "State 44 command 4 blocks at countdown $10 without changing state");
    const auto pointer = speech.state().city.sprites.pointers[1];
    check(pointer == 0x3C && speech.registers().beam_phase79 == 0 &&
              speech.state().city.sprites.y_expand_mask == 0x0F,
          "State 44 beam helper wraps phase and selects mirrored pointer base");
    check(!speech.resume_speech().blocking_request && speech.raw_state() == 0x2C,
          "command 4 continuation returns without inventing a frame tick");

    auto reward_state = state(0x2C);
    reward_state.city.countdown7c = 0;
    reward_state.city.balance57 = {0x12, 0x60, 0x34};
    reward_state.city.sprites.x[5] = 0x5E;
    reward_state.city.sprites.x[6] = 0x84;
    reward_state.city.sprites.y[5] = reward_state.city.sprites.y[6] = 0x80;
    Zuul reward(payload, reward_state, b, registers());
    (void)reward.tick();
    check(reward.raw_state() == 0x36 && reward.state().city.balance57.byte57 == 0x13 &&
              reward.state().city.balance57.byte58 == 0x10 &&
              reward.state().city.balance57.byte59 == 0x34,
          "countdown-zero State 44 adds packed-BCD $005000 and exposes raw State 54");
    check(reward.registers().scratch23 == 0 &&
              reward.registers().scratch24 == 0x50 &&
              reward.registers().scratch25 == 0,
          "State 44 leaves exact $23/$24/$25 reward operands");

    auto overflow_state = reward_state;
    overflow_state.city.balance57 = {0x99, 0x80, 0x77};
    Zuul overflow(payload, overflow_state, b, registers());
    (void)overflow.tick();
    check(overflow.state().city.balance57.byte57 == 0x99 &&
              overflow.state().city.balance57.byte58 == 0x99 &&
              overflow.state().city.balance57.byte59 == 0x77,
          "$005000 overflow caps only the two upper balance bytes");
}

void test_validation(const Payload& payload)
{
    try {
        Zuul invalid(payload, state(0x2D), building(), registers());
        (void)invalid;
        check(false, "constructor rejects State 45 owned by ending package");
    } catch (const std::invalid_argument&) {
        check(true, "constructor rejects State 45 owned by ending package");
    }
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_state40_initializes_rooftop(payload);
        test_state41_collision_speech_and_resume(payload);
        test_state41_gates_and_unconditional_latch_read(payload);
        test_state42_and_state43_scene_work(payload);
        test_state44_speech_reward_and_raw_handoff(payload);
        test_validation(payload);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
    if (failures == 0) std::cout << "Zuul tests passed\n";
    return failures == 0 ? 0 : 1;
}
