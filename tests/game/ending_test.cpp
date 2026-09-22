#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/account_codec.hpp"
#include "game/ending.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::DriveControlsState;
using ghostbusters::game::Ending;
using ghostbusters::game::EndingAudioAction;
using ghostbusters::game::EndingAudioEvent;
using ghostbusters::game::EndingInput;
using ghostbusters::game::EndingRegisters;
using ghostbusters::game::EndingTickResult;

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

DriveControlsState make_state(std::uint8_t handler) {
    DriveControlsState state;
    state.city.state3a = handler;
    state.city.key17 = 0x66;
    state.city.starting_balance51 = {0x00, 0x95, 0x00};
    state.city.balance57 = {0x01, 0x23, 0x45};
    state.city.characters.screen.fill(0xA5);
    state.city.characters.colors.fill(0x0D);
    state.city.sprites.x.fill(0x31);
    state.city.sprites.y.fill(0x32);
    state.city.sprites.target_x.fill(0x41);
    state.city.sprites.target_y.fill(0x42);
    return state;
}

std::array<std::uint8_t, 20> saved_name(std::string_view value) {
    std::array<std::uint8_t, 20> result{};
    std::copy(value.begin(), value.end(), result.begin());
    return result;
}

EndingRegisters registers() {
    EndingRegisters result;
    result.cursor_column38 = 1;
    result.cursor_row39 = 1;
    result.post_failure_ea7a = 0x91;
    result.gate02 = 0x80;
    result.scratch23_26 = {0x81, 0x82, 0x83, 0x84};
    result.scratch27 = 0x85;
    result.runtime_text_ea14.fill(0xCC);
    return result;
}

void append(std::vector<EndingAudioEvent> &target, const EndingTickResult &result) {
    target.insert(target.end(), result.audio_events.begin(), result.audio_events.end());
}

EndingTickResult run_until(Ending &ending, std::uint8_t state, bool script_active,
                           std::vector<EndingAudioEvent> &events) {
    for (unsigned count = 0; count < 4096; ++count) {
        if (ending.raw_state() == state && ending.script_active() == script_active)
            return {};
        auto result = ending.tick({0, 0xFF});
        append(events, result);
        if (result.blocking_request || result.restart_requested)
            return result;
    }
    throw std::runtime_error("Ending test did not reach requested state");
}

void test_reset_and_poor_path(const Payload &payload) {
    auto initial = make_state(45);
    std::array<std::uint8_t, 64> old_screen_tail{};
    old_screen_tail.fill(0xA5);
    Ending ending(payload, initial, saved_name("VENKMAN,PETER"), registers());
    const auto first = ending.tick();
    check(first.audio_events ==
                  std::vector<EndingAudioEvent>{{EndingAudioAction::ending_reset, 0}} &&
              ending.raw_state() == 59 && ending.active_script_id() == 0x0D,
          "State 45 resets, starts foreclosure text, and jumps directly to State "
          "59");
    const auto &reset = ending.state().city;
    check(std::all_of(reset.characters.screen.begin(), reset.characters.screen.begin() + 0x3C0,
                      [](auto value) { return value == 0; }) &&
              std::equal(reset.characters.screen.begin() + 0x3C0, reset.characters.screen.end(),
                         old_screen_tail.begin()),
          "$8D96 preserves the final 64 screen bytes");
    check(std::all_of(reset.characters.colors.begin() + 0x370,
                      reset.characters.colors.begin() + 0x397,
                      [](auto value) { return value == 1; }) &&
              reset.characters.colors[0x36F] == 0 && reset.characters.colors[0x397] == 0,
          "$95B9 applies the State-17+ color strip after clearing");
    check(reset.characters.background == 8 &&
              std::all_of(reset.sprites.x.begin(), reset.sprites.x.end(),
                          [](auto value) { return value == 0; }) &&
              std::all_of(reset.sprites.y.begin(), reset.sprites.y.end(),
                          [](auto value) { return value == 0; }) &&
              reset.sprites.target_x[0] == 0x41 && reset.sprites.target_y[0] == 0x42,
          "$8D96 changes background/current coordinates but retains targets");

    std::vector<EndingAudioEvent> events = first.audio_events;
    (void)run_until(ending, 57, false, events);
    const auto blocked = ending.tick();
    append(events, blocked);
    check(blocked.blocking_request && ending.speech_blocked() &&
              blocked.audio_events ==
                  std::vector<EndingAudioEvent>{{EndingAudioAction::speech_start, 3}},
          "poor ending alone requests blocking speech command 3");
    check(ending.state().city.balance57 == initial.city.balance57 &&
              ending.registers().temporary_balance54 == initial.city.balance57,
          "poor ending restores ending balance after displaying starting balance");
    check(ending.registers().runtime_text_length == 7 &&
              std::equal(ending.registers().runtime_text_ea14.begin(),
                         ending.registers().runtime_text_ea14.begin() + 7,
                         std::array<std::uint8_t, 7>{'$', '1', '2', '3', '4', '5', 0xFF}.begin()),
          "poor ending exposes the exact final runtime money script");
    check(ending.resume_speech().audio_events.empty() && ending.raw_state() == 58 &&
              ending.registers().gate02 == 0xFF && ending.state().city.key17 == 0,
          "speech continuation sets gate $02 and advances through $8D86");
}

void test_failure_name_credit_and_account(const Payload &payload) {
    auto initial = make_state(46);
    Ending ending(payload, initial, saved_name("VENKMAN,PETER"), registers());
    std::vector<EndingAudioEvent> events;
    append(events, ending.tick());
    run_until(ending, 48, true, events);
    const std::array<std::uint8_t, 14> display_name{'P', 'E', 'T', 'E', 'R', ' ', 'V',
                                                    'E', 'N', 'K', 'M', 'A', 'N', 0xFF};
    check(ending.active_script_id() == 0x80 &&
              ending.registers().runtime_text_length == display_name.size() &&
              std::equal(display_name.begin(), display_name.end(),
                         ending.registers().runtime_text_ea14.begin()),
          "$9B90 runtime name is FIRST LAST with an exact FF terminator");

    run_until(ending, 51, true, events);
    check(ending.state().city.balance57 ==
                  ghostbusters::game::AccountBalanceBytes{0x01, 0x23, 0x00} &&
              ending.registers().scratch23_26[0] == 0x01 &&
              ending.registers().scratch23_26[1] == 0x23 &&
              ending.registers().scratch23_26[2] == 0 && ending.registers().scratch27 == 3,
          "State 50 clears only balance low byte and exposes money scratch");

    run_until(ending, 53, true, events);
    const auto expected_account =
        ghostbusters::game::encodeAccount(saved_name("VENKMAN,PETER"), {0x01, 0x23, 0});
    check(ending.registers().encoded_account_eac7 == expected_account &&
              ending.registers().scratch23_26 == expected_account &&
              ending.registers().scratch27 == 4 &&
              ending.registers().runtime_text_length == 9,
          "State 52 exposes encoded account bytes and its eight-digit runtime "
          "text");
    for (std::size_t index = 0; index < 4; ++index) {
        check(ending.registers().runtime_text_ea14[index * 2] ==
                      static_cast<std::uint8_t>('0' | (expected_account[index] >> 4U)) &&
                  ending.registers().runtime_text_ea14[index * 2 + 1] ==
                      static_cast<std::uint8_t>('0' | (expected_account[index] & 0x0F)),
              "account runtime text expands both packed nibbles");
    }
    check(ending.registers().runtime_text_ea14[8] == 0xFF, "account runtime text is FF terminated");

    const auto completed = run_until(ending, 58, false, events);
    check(!completed.blocking_request && !ending.speech_blocked() &&
              ending.registers().gate02 == 0xFF,
          "collision failure reaches input wait without ending speech");
    check(std::count(events.begin(), events.end(),
                     EndingAudioEvent{EndingAudioAction::ending_reset, 0}) == 1 &&
              std::none_of(events.begin(), events.end(),
                           [](const auto &event) {
                               return event.action == EndingAudioAction::speech_start;
                           }) &&
              std::any_of(
                  events.begin(), events.end(),
                  [](const auto &event) { return event.action == EndingAudioAction::text_tone; }),
          "failure emits one reset and text tones but no speech");
}

void test_reward_path_and_raw_restart(const Payload &payload) {
    auto initial = make_state(54);
    initial.city.balance57 = {0x01, 0x50, 0x77};
    Ending ending(payload, initial, saved_name("SPENGLER,EGON"), registers());
    std::vector<EndingAudioEvent> events;
    append(events, ending.tick());
    check(ending.raw_state() == 55 && ending.active_script_id() == 0x18,
          "State 54 starts the rewarded ending at script 24");
    run_until(ending, 49, true, events);
    check(ending.active_script_id() == 0x19,
          "reward name path joins shared credit path after script 25");
    run_until(ending, 58, false, events);
    check(!ending.speech_blocked() && ending.state().city.balance57.byte59 == 0,
          "rewarded ending skips speech and shares State-50 low-byte clearing");
    check(!ending.tick({0, 0x21}).restart_requested && ending.tick({0, 0x20}).restart_requested &&
              ending.tick({0, 0x28}).restart_requested,
          "State 58 accepts only held raw F1/F3 matrix positions");
}

void test_handler_callback_and_validation(const Payload &payload) {
    Ending ending(payload, make_state(59), saved_name("STANTZ,RAY"), registers());
    unsigned callbacks = 0;
    (void)ending.tick({}, [&](DriveControlsState &state) {
        ++callbacks;
        state.city.backup_men3d = 4;
    });
    check(callbacks == 1 && ending.state().city.backup_men3d == 4 && ending.raw_state() == 60 &&
              ending.script_active(),
          "script-idle tick invokes common-after callback before handler");
    for (unsigned count = 0; count < 4; ++count)
        (void)ending.tick({1, 0xFF}, [&](DriveControlsState &) { ++callbacks; });
    check(callbacks == 1, "active script with no due byte does not invoke common-after callback");
    for (unsigned count = 0; count < 256 && !(ending.raw_state() == 61 && ending.script_active());
         ++count)
        (void)ending.tick({0, 0xFF}, [&](DriveControlsState &) { ++callbacks; });
    check(callbacks == 2 && ending.raw_state() == 61,
          "FF tick invokes callback once before dispatching the next handler");

    try {
        Ending invalid(payload, make_state(44), saved_name("ZEDMORE,WINSTON"), registers());
        (void)invalid;
        check(false, "constructor rejects State 44 owned by Zuul");
    } catch (const std::invalid_argument &) {
        check(true, "constructor rejects State 44 owned by Zuul");
    }
}

void test_money_workspace_tail(const Payload &payload) {
    auto initial_registers = registers();
    initial_registers.temporary_balance54 = {0x00, 0x43, 0x21};
    initial_registers.runtime_text_ea14.fill(0xFF);
    Ending ending(payload, make_state(62), saved_name("SPENGLER,EGON"), initial_registers);

    (void)ending.tick();
    const std::array<std::uint8_t, 8> expected{'$', '4', '3', '2', '1', 0xFF, '1', 0xFF};
    check(ending.registers().runtime_text_length == 6 &&
              std::equal(expected.begin(), expected.end(),
                         ending.registers().runtime_text_ea14.begin()),
          "$9D24/$9063 compaction retains the final raw digit after the moved terminator");

    auto zero_registers = registers();
    zero_registers.temporary_balance54 = {};
    zero_registers.runtime_text_ea14.fill(0xFF);
    Ending zero(payload, make_state(62), saved_name("SPENGLER,EGON"), zero_registers);
    (void)zero.tick();
    const std::array<std::uint8_t, 8> expected_zero{'$',  '0', 0xFF, 0x20,
                                                   0x20, '$', '0',  0xFF};
    check(zero.registers().runtime_text_length == 3 &&
              std::equal(expected_zero.begin(), expected_zero.end(),
                         zero.registers().runtime_text_ea14.begin()),
          "zero money text retains spaces and the uncompressed $0 workspace tail");
}

} // namespace

int main() {
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_reset_and_poor_path(payload);
        test_failure_name_credit_and_account(payload);
        test_reward_path_and_raw_restart(payload);
        test_handler_callback_and_validation(payload);
        test_money_workspace_tail(payload);
    } catch (const std::exception &error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
    if (failures == 0)
        std::cout << "Ending tests passed\n";
    return failures == 0 ? 0 : 1;
}
