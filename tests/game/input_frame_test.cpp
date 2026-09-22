#include "game/input_frame.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using ghostbusters::game::CommonVolume;
using ghostbusters::game::GameReset;
using ghostbusters::game::InputFrameResult;
using ghostbusters::game::InputFrameState;
using ghostbusters::game::update_input_frame;

void check(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void check_result(const InputFrameResult& result, const bool continue_frame,
                  const CommonVolume volume, const GameReset reset,
                  const std::string& context)
{
    check(result.continue_frame == continue_frame,
          context + ": unexpected continue_frame");
    check(result.volume == volume, context + ": unexpected volume action");
    check(result.reset == reset, context + ": unexpected reset action");
}

void test_initial_state_and_open_frame()
{
    InputFrameState state;
    check(state.gate02 == 0x80 && state.cycle03 == 0 && state.value04 == 0 &&
              state.value05 == 0 && state.frame09 == 0 && state.delay14 == 0 &&
              state.mode20 == 2 && state.idle45 == 0 && state.idle46 == 0 &&
              state.flags47 == 0,
          "InputFrameState defaults do not describe the initialized foreground");

    const auto result = update_input_frame(state, 0x01, 0x11, 0xFF);
    check_result(result, true, CommonVolume::restore_cache, GameReset::none,
                 "initialized open frame");
    check(state.frame09 == 1 && state.delay14 == 0 && state.gate02 == 0x80 &&
              state.idle45 == 0 && state.idle46 == 0 && state.flags47 == 0,
          "an initialized frame changed unrelated state");
}

void test_raw_key_gate_boundaries()
{
    // $3F is evaluated only for states $12-$29. State $11 takes the ordinary
    // key path, and state $2A is already beyond the pause gate.
    {
        InputFrameState state;
        const auto result = update_input_frame(state, 0x01, 0x11, 0x3F);
        check_result(result, true, CommonVolume::restore_cache, GameReset::none,
                     "raw $3F at State $11");
        check(state.flags47 == 0 && state.frame09 == 1,
              "State $11 did not clear the pause bit and advance the frame");
    }
    {
        InputFrameState state;
        const auto result = update_input_frame(state, 0x01, 0x12, 0x3F);
        check_result(result, false, CommonVolume::mute, GameReset::none,
                     "raw $3F at State $12");
        check(state.flags47 == 0xC0 && state.frame09 == 0,
              "State $12 did not enter the pause gate before the frame step");
    }
    {
        InputFrameState state;
        const auto result = update_input_frame(state, 0x01, 0x29, 0x3F);
        check_result(result, false, CommonVolume::mute, GameReset::none,
                     "raw $3F at State $29");
        check(state.flags47 == 0xC0 && state.frame09 == 0,
              "State $29 did not enter the pause gate");
    }
    {
        InputFrameState state;
        const auto result = update_input_frame(state, 0x01, 0x2A, 0x3F);
        check_result(result, true, CommonVolume::unchanged, GameReset::none,
                     "raw $3F at State $2A");
        check(state.flags47 == 0 && state.frame09 == 1,
              "State $2A incorrectly used the pre-$2A raw-key gate");
    }
}

void test_press_hold_release_repress_sequence()
{
    InputFrameState state;
    state.frame09 = 0xA5;

    const auto press = update_input_frame(state, 0x01, 0x12, 0x3F);
    check_result(press, false, CommonVolume::mute, GameReset::none,
                 "pause press");
    check(state.flags47 == 0xC0 && state.frame09 == 0xA5,
          "pause press did not latch and stop before $09");

    const auto hold = update_input_frame(state, 0x02, 0x12, 0x3F);
    check_result(hold, false, CommonVolume::unchanged, GameReset::none,
                 "pause hold");
    check(state.flags47 == 0xC0 && state.frame09 == 0xA5,
          "pause hold changed the latched gate or frame");

    const auto release = update_input_frame(state, 0x03, 0x12, 0xFF);
    check_result(release, false, CommonVolume::unchanged, GameReset::none,
                 "pause release");
    check(state.flags47 == 0x80 && state.frame09 == 0xA5,
          "pause release did not clear only the raw-key latch bit");

    const auto repress = update_input_frame(state, 0x04, 0x12, 0x3F);
    check_result(repress, true, CommonVolume::mute, GameReset::none,
                 "pause repress");
    check(state.flags47 == 0x40 && state.frame09 == 0xA6,
          "pause repress did not reopen the foreground frame");
}

void test_flags_lower_bits_and_existing_latches()
{
    {
        InputFrameState state;
        state.flags47 = 0x35;
        const auto result = update_input_frame(state, 0x01, 0x12, 0xFF);
        check_result(result, true, CommonVolume::restore_cache, GameReset::none,
                     "ordinary key with lower flags");
        check(state.flags47 == 0x35,
              "ordinary key did not preserve flags $47 lower six bits");
    }
    {
        InputFrameState state;
        state.flags47 = 0x05;
        const auto result = update_input_frame(state, 0x01, 0x12, 0x3F);
        check_result(result, false, CommonVolume::mute, GameReset::none,
                     "pause key with lower flags");
        check(state.flags47 == 0xC5,
              "pause key did not preserve flags $47 lower six bits");
    }
    {
        InputFrameState state;
        state.flags47 = 0x45; // bit 6 is the already-tested raw-$3F latch.
        const auto result = update_input_frame(state, 0x01, 0x12, 0x3F);
        check_result(result, true, CommonVolume::unchanged, GameReset::none,
                     "held raw key with existing latch");
        check(state.flags47 == 0x45 && state.frame09 == 1,
              "an already latched raw key toggled the pause gate");
    }
    {
        InputFrameState state;
        state.flags47 = 0x80;
        const auto result = update_input_frame(state, 0x01, 0x2A, 0xFF);
        check_result(result, false, CommonVolume::unchanged, GameReset::none,
                     "existing pause gate at State $2A");
        check(state.flags47 == 0x80 && state.frame09 == 0,
              "bit 7 of flags $47 did not remain a foreground gate");
    }
}

void test_counter_wrap_and_idle_counters()
{
    {
        InputFrameState state;
        state.gate02 = 0;
        state.cycle03 = 7;
        state.frame09 = 0xFF;
        state.idle45 = 2;
        state.idle46 = 0xFE;

        const auto nonzero = update_input_frame(state, 0xFF, 0x11, 0xFF);
        check_result(nonzero, false, CommonVolume::restore_cache, GameReset::none,
                     "nonzero counter before wrap");
        check(state.cycle03 == 7 && state.idle45 == 2 && state.idle46 == 0xFE &&
                  state.frame09 == 0,
              "nonzero counter incorrectly ran the zero-counter path");

        const auto wrapped = update_input_frame(state, 0x00, 0x11, 0xFF);
        check_result(wrapped, false, CommonVolume::unchanged, GameReset::partial,
                     "counter-zero cycle wrap");
        check(state.cycle03 == 0 && state.gate02 == 0xC0 && state.frame09 == 0,
              "cycle wrap did not request the partial reset before $09");
        check(state.idle45 == 2 && state.idle46 == 0xFE,
              "cycle-wrap reset consumed the idle counters");
    }
    {
        InputFrameState state;
        state.idle45 = 3;
        state.idle46 = 0xFF;
        state.frame09 = 0xFF;

        const auto result = update_input_frame(state, 0x00, 0x11, 0xFF);
        check_result(result, true, CommonVolume::restore_cache, GameReset::none,
                     "idle counter wrap");
        check(state.idle46 == 0 && state.idle45 == 0x80 && state.frame09 == 0,
              "idle counters did not advance at the $46 wrap");
    }
}

void test_full_reset_initialization()
{
    InputFrameState state;
    state.gate02 = 0;
    state.cycle03 = 7;
    state.value04 = 0xA4;
    state.value05 = 0xB5;
    state.frame09 = 0xFE;
    state.delay14 = 0x22;
    state.mode20 = 1;
    state.idle45 = 0x03;
    state.idle46 = 0xF0;
    state.flags47 = 0x65;

    const auto result = update_input_frame(state, 0x00, 0x29, 0x3F);
    check_result(result, false, CommonVolume::unchanged, GameReset::full,
                 "full reset initialization");
    check(state.mode20 == 2 && state.gate02 == 0x80 && state.cycle03 == 0 &&
              state.value04 == 0 && state.value05 == 0,
          "full reset did not initialize its owned control bytes");
    check(state.frame09 == 0xFE && state.delay14 == 0x22 && state.idle45 == 0x03 &&
              state.idle46 == 0xF0 && state.flags47 == 0x65,
          "full reset overwrote bytes owned by the surrounding scheduler");
}

void test_delay_and_frame_wrap()
{
    InputFrameState state;
    state.frame09 = 0xFF;
    state.delay14 = 2;

    const auto first = update_input_frame(state, 0x01, 0x12, 0xFF);
    check_result(first, false, CommonVolume::restore_cache, GameReset::none,
                 "first reset-delay tick");
    check(state.delay14 == 1 && state.frame09 == 0,
          "first reset-delay tick did not retain $14 and wrap $09");

    const auto second = update_input_frame(state, 0x02, 0x12, 0xFF);
    check_result(second, false, CommonVolume::restore_cache, GameReset::partial,
                 "terminal reset-delay tick");
    check(state.delay14 == 0 && state.frame09 == 1,
          "terminal reset-delay tick did not report the cleared $14");

    const auto resumed = update_input_frame(state, 0x03, 0x12, 0xFF);
    check_result(resumed, true, CommonVolume::restore_cache, GameReset::none,
                 "post-delay frame");
    check(state.delay14 == 0 && state.frame09 == 2,
          "post-delay frame did not resume after the reset request");
}

} // namespace

int main()
{
    try {
        test_initial_state_and_open_frame();
        test_raw_key_gate_boundaries();
        test_press_hold_release_repress_sequence();
        test_flags_lower_bits_and_existing_latches();
        test_counter_wrap_and_idle_counters();
        test_full_reset_initialization();
        test_delay_and_frame_wrap();
        std::cout << "input-frame tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
