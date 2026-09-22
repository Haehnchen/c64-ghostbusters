#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/input_frame.hpp"
#include "game/keyboard_scanner.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::InputFrameState;
using ghostbusters::game::KeyboardScannerInput;
using ghostbusters::game::KeyboardScannerState;
using ghostbusters::game::poll_input;

void check(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

KeyboardScannerInput key(const std::uint8_t raw)
{
    return KeyboardScannerInput::key(raw);
}

void test_stateful_press_hold_release(const Payload& payload)
{
    InputFrameState frame;
    frame.idle45 = 3;
    frame.idle46 = 7;
    KeyboardScannerState state;

    const auto press = poll_input(payload, frame, state, key(0x21)); // Z
    check(press.keyboard_scanned, "ordinary press did not complete CIA scan");
    check(state.raw19 == 0x21 && state.latch18 == 0x5A && state.key17 == 0x5A,
          "ordinary press did not produce the translated edge");
    check(frame.idle45 == 0 && frame.idle46 == 0,
          "ordinary press did not clear the shared idle counters");

    state.key17 = 0xA5; // A consumer-owned event left pending.
    const auto hold = poll_input(payload, frame, state, key(0x21));
    check(hold.keyboard_scanned && state.raw19 == 0x21 && state.latch18 == 0x5A,
          "held key changed its raw/latch state");
    check(state.key17 == 0xA5, "held key generated a second translated event");

    const auto release = poll_input(payload, frame, state, {});
    check(release.keyboard_scanned && state.raw19 == 0xFF && state.latch18 == 0,
          "release did not clear raw key and translated latch");
    check(state.key17 == 0xA5, "release consumed the pending translated event");

    const auto repress = poll_input(payload, frame, state, key(0x21));
    check(repress.keyboard_scanned && state.raw19 == 0x21 && state.latch18 == 0x5A && state.key17 == 0x5A,
          "repress did not produce a fresh translated edge");
}

void test_joystick_guards_and_gate(const Payload& payload)
{
    {
        InputFrameState frame;
        frame.idle45 = 9;
        frame.idle46 = 10;
        KeyboardScannerState state;
        state.key17 = 0xA5;
        state.latch18 = 0x44;
        state.raw19 = 0x12;

        auto input = key(0x21);
        input.port_b = 0xEF; // fire active-low on CIA Port B.
        const auto result = poll_input(payload, frame, state, input);
        check(result.joystick_active && !result.keyboard_scanned,
              "active Port-B joystick did not stop the keyboard guard");
        check(state.joystick33 == 0x0F && state.joystick34 == 0x1F,
              "joystick poll did not retain the five active-low Port-B bits");
        check(frame.idle45 == 0 && frame.idle46 == 0,
              "active joystick did not clear the shared idle counters");
        check(state.key17 == 0xA5 && state.latch18 == 0x44 && state.raw19 == 0x12,
              "Port-B joystick guard modified keyboard state");
    }
    {
        InputFrameState frame;
        KeyboardScannerState state;
        state.key17 = 0xA5;
        state.latch18 = 0x44;
        state.raw19 = 0x12;
        auto input = key(0x00); // Port-A bit 0 low reaches Port-B column 0.
        input.port_a = 0xFE;
        const auto result = poll_input(payload, frame, state, input);
        check(result.joystick_active && !result.keyboard_scanned,
              "active Port-A joystick did not stop the keyboard guard");
        check(state.joystick33 == 0x1E && state.key17 == 0xA5 &&
                  state.latch18 == 0x44 && state.raw19 == 0x12,
              "Port-A contact did not reach the joystick read or changed keyboard state");
    }
    {
        InputFrameState frame;
        frame.gate02 = 0;
        KeyboardScannerState state;
        state.joystick33 = 0x0E;
        state.joystick34 = 0x12;
        const auto result = poll_input(payload, frame, state, key(0x21));
        check(result.keyboard_scanned && state.joystick33 == 0x1F &&
                  state.joystick34 == 0x1F,
              "$02 gate did not suppress both joystick bytes");
        check(state.key17 == 0x5A && state.latch18 == 0x5A,
              "$02 gate incorrectly disabled keyboard scanning");
    }
    {
        InputFrameState frame;
        KeyboardScannerState state;
        state.joystick34 = 0x12;
        (void)poll_input(payload, frame, state, {});
        check(state.joystick33 == 0x1F && state.joystick34 == 0x12,
              "open $02 gate changed the original persistent $34 byte");
    }
}

void test_idle_and_duplicate_translations(const Payload& payload)
{
    InputFrameState frame;
    KeyboardScannerState state;
    frame.idle45 = 1;
    frame.idle46 = 2;

    // Raw $36 and $2E both translate to $3D.  The later scan of the smaller
    // raw index wins, while the equal translated latch suppresses a repeat.
    auto first = key(0x36);
    (void)poll_input(payload, frame, state, first);
    check(state.raw19 == 0x36 && state.key17 == 0x3D && state.latch18 == 0x3D,
          "first duplicate translated key did not press");

    state.key17 = 0xA5;
    auto both = key(0x36);
    both.keys[0x2E] = true;
    (void)poll_input(payload, frame, state, both);
    check(state.raw19 == 0x2E && state.latch18 == 0x3D && state.key17 == 0xA5,
          "duplicate translated code did not use smallest raw position/latch");
    check(frame.idle45 == 0 && frame.idle46 == 0,
          "initial translated press did not clear idle counters");

    frame.idle45 = 4;
    frame.idle46 = 5;
    (void)poll_input(payload, frame, state, key(0x36));
    check(state.raw19 == 0x36 && state.key17 == 0xA5 && frame.idle45 == 4 &&
              frame.idle46 == 5,
          "equal translated latch was treated as a new key/idle activity");

    (void)poll_input(payload, frame, state, {});
    check(state.raw19 == 0xFF && state.latch18 == 0 && state.key17 == 0xA5,
          "duplicate-key release did not preserve pending $17");
}

void test_special_keys_and_filtered_positions(const Payload& payload)
{
    for (const auto raw : {std::uint8_t{0x20}, std::uint8_t{0x28},
                           std::uint8_t{0x3F}}) {
        InputFrameState frame;
        frame.idle45 = 6;
        frame.idle46 = 7;
        KeyboardScannerState state;
        state.key17 = 0xA5;
        state.latch18 = 0x44;
        const auto result = poll_input(payload, frame, state, key(raw));
        check(result.keyboard_scanned && state.raw19 == raw && state.latch18 == 0 &&
                  state.key17 == 0xA5,
              "F1/F3/RUN-STOP special key did not clear only $18");
        check(frame.idle45 == 6 && frame.idle46 == 7,
              "special key incorrectly cleared idle counters");
    }

    InputFrameState frame;
    KeyboardScannerState state;
    state.key17 = 0xA5;
    state.latch18 = 0x44;
    state.raw19 = 0x12;
    const auto result = poll_input(payload, frame, state, key(0x06)); // $21 filter.
    check(result.keyboard_scanned && state.raw19 == 0xFF && state.latch18 == 0 &&
              state.key17 == 0xA5,
          "filtered translation $21 became a raw or translated event");
}

void test_all_64_translation_entries(const Payload& payload)
{
    const auto table = payload.asset("ui/keyboard");
    check(table.size() == 64, "keyboard translation table is not 64 bytes");
    for (std::uint8_t raw = 0; raw < 64; ++raw) {
        InputFrameState frame;
        KeyboardScannerState state;
        (void)poll_input(payload, frame, state, key(raw));
        const auto translated = table[raw];
        if (translated == 0x21) {
            check(state.raw19 == 0xFF && state.latch18 == 0,
                  "table filter entry was accepted");
        } else if (translated == 0x2F) {
            check(state.raw19 == raw && state.latch18 == 0,
                  "special table entry did not clear its latch");
        } else {
            check(state.raw19 == raw && state.key17 == translated &&
                      state.latch18 == translated,
                  "ordinary table entry translated to the wrong event");
        }
    }
}

void test_matrix_contact_ghost(const Payload& payload)
{
    // The live CIA capture uses A ($11), D ($12), and R ($0A): three corners
    // of (rows 1,2) x (columns 1,2).  The missing W position ($09) is visible
    // to a diode-less scan and wins the original descending index selection.
    auto input = key(0x11); // column 2, row 1 (A)
    input.keys[0x12] = true; // column 2, row 2 (D)
    input.keys[0x0A] = true; // column 1, row 2 (R)
    InputFrameState frame;
    KeyboardScannerState state;
    (void)poll_input(payload, frame, state, input);
    check(state.raw19 == 0x09,
          "CIA contact simulation did not expose the rectangle ghost position");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_stateful_press_hold_release(payload);
        test_joystick_guards_and_gate(payload);
        test_idle_and_duplicate_translations(payload);
        test_special_keys_and_filtered_positions(payload);
        test_all_64_translation_entries(payload);
        test_matrix_contact_ghost(payload);
        std::cout << "keyboard-scanner tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
