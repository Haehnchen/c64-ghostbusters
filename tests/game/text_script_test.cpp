#include "assets/payload.hpp"
#include "game/text_script.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ghostbusters::assets::Payload;
using ghostbusters::game::SoundEvent;
using ghostbusters::game::TextScript;
using ghostbusters::video::CharacterFrame;

constexpr std::size_t kScreenColumns = 40;
constexpr std::size_t kScreenRows = 25;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Function>
void expectOutOfRange(Function&& function, const std::string& message)
{
    try {
        function();
        check(false, message + " (no exception)");
    } catch (const std::out_of_range&) {
    } catch (const std::exception& error) {
        check(false, message + " (wrong exception: " + error.what() + ")");
    }
}

template <typename Function>
void expectRuntimeError(Function&& function, const std::string& message)
{
    try {
        function();
        check(false, message + " (no exception)");
    } catch (const std::runtime_error&) {
    } catch (const std::exception& error) {
        check(false, message + " (wrong exception: " + error.what() + ")");
    }
}

void testCadenceAndCommands(std::span<const std::uint8_t> commands)
{
    CharacterFrame frame;
    frame.screen.fill(0xAA);
    frame.colors.fill(0xBB);
    TextScript script;
    script.start(commands, 31, 2, 0);

    script.tick(frame, 1); // cadence mask 3: not selected
    check(script.column() == 31 && script.row() == 2 && frame.screen[111] == 0xAA,
          "unselected frames consume no source byte");
    script.tick(frame, 4); // space: writes blank and wraps
    check(frame.screen[2 * kScreenColumns + 31] == 0 && script.column() == 1 && script.row() == 3,
          "space at column 31 writes code zero and autowraps");
    script.tick(frame, 8); // uppercase A -> code 1
    check(frame.screen[121] == 1 && frame.colors[121] == 0 && script.column() == 2,
          "uppercase bytes subtract $40 and write the selected color");
    script.tick(frame, 12); // zero: no output
    check(script.column() == 2 && frame.screen[122] == 0xAA,
          "zero source bytes do not write a glyph");
    script.tick(frame, 16); // negative control: set column 5
    check(script.column() == 5 && frame.screen[122] == 0xAA,
          "negative source bytes set column without writing");
    script.tick(frame, 20); // newline
    check(script.column() == 1 && script.row() == 4,
          "carriage return moves to column 1 on the next row");
    script.tick(frame, 24); // B -> code 2
    check(frame.screen[161] == 2 && script.column() == 2,
          "ordinary character bytes are written directly");
    script.tick(frame, 28); // FF
    check(!script.active(), "FF terminates the script");
    const auto before = frame.screen;
    script.tick(frame, 32);
    check(frame.screen == before, "inactive scripts consume no further bytes");
}

void testSoundEvents(std::span<const std::uint8_t> commands)
{
    CharacterFrame frame;
    TextScript script;
    script.start(commands, 0, 0, 0);

    script.tick(frame, 0, 3); // space
    check(script.take_sound_events().empty(),
          "spaces do not trigger a text tone");
    script.tick(frame, 0, 3); // A
    const auto glyph_events = script.take_sound_events();
    check(glyph_events.size() == 1 && glyph_events[0] == SoundEvent::text_tone,
          "normal printable bytes trigger one text tone");
    check(script.take_sound_events().empty(),
          "taking script sound events drains the queue");

    script.tick(frame, 0, 3); // zero
    check(script.take_sound_events().empty(),
          "zero source bytes do not trigger a text tone");
    script.tick(frame, 0, 3); // cursor control $85
    const auto cursor_events = script.take_sound_events();
    check(cursor_events.size() == 1 && cursor_events[0] == SoundEvent::text_tone &&
              script.column() == 5,
          "cursor-control bytes trigger a tone while still moving the cursor");
    script.tick(frame, 0, 3); // CR
    check(script.take_sound_events().empty(),
          "carriage return does not trigger a text tone");
    script.tick(frame, 0, 3); // B
    check(script.take_sound_events().size() == 1,
          "ordinary source bytes continue to trigger tones after cursor control");
    script.tick(frame, 0, 3); // FF
    check(script.take_sound_events().empty() && !script.active(),
          "FF ends a script without a text tone");

    TextScript fast;
    CharacterFrame fast_frame;
    const std::array<std::uint8_t, 3> fast_source{0x41, 0x42, 0xFF};
    fast.start(fast_source, 0, 0, 0);
    fast.tick(fast_frame, 0, 0);
    check(fast.take_sound_events().empty(),
          "the cadence-zero fast batch emits no text tones");
}

void testOnlySpacesAutowrap(std::span<const std::uint8_t> spaces)
{
    CharacterFrame frame;
    TextScript script;
    script.start(spaces, 31, 0, 0);
    script.tick(frame, 0, 3); // A at col 31: no wrap.
    check(script.column() == 32 && script.row() == 0,
          "a non-space at column 31 does not autowrap");
    script.tick(frame, 0, 3); // space at col 32: wrap.
    check(script.column() == 1 && script.row() == 1,
          "a space after column 31 is the autowrap trigger");

    TextScript colored;
    CharacterFrame coloredFrame;
    colored.start(spaces, 31, 0, 1);
    colored.tick(coloredFrame, 0, 3); // A at col 31: no wrap.
    colored.tick(coloredFrame, 0, 3); // color 1 disables the wrap.
    check(colored.column() == 33 && colored.row() == 0 &&
              coloredFrame.screen[32] == 0 && coloredFrame.colors[32] == 1,
          "a space with nonzero ink color does not autowrap");
}

void testFastBatch(std::span<const std::uint8_t> source)
{
    CharacterFrame frame;
    frame.screen.fill(0xAA);
    TextScript script;
    script.start(source, 1, 13, 0);
    for (unsigned tick = 0; tick < 3; ++tick) {
        script.tick(frame, 0, 0);
        check(script.active() && script.column() == 1 + 40 * (tick + 1) && script.row() == 13,
              "fast text consumes 40 bytes per frame without wrapping its cursor");
    }
    check(frame.screen[520] == 0xAA && frame.screen[521] == 0 && frame.screen[640] == 0 &&
              frame.screen[641] == 0xAA, "fast clear writes exactly 120 linear cells");
    script.tick(frame, 0, 0);
    check(!script.active(), "fast clear consumes FF on its fourth frame");
}

void testRuntimeSource(std::span<const std::uint8_t> commands)
{
    CharacterFrame frame;
    frame.screen.fill(0xAA);
    TextScript script;
    {
        std::vector<std::uint8_t> source{0x41, 0x42, 0xFF, 0x43};
        script.start(source, 0, 0, 2);
        source[0] = 0x20;
    }
    script.tick(frame, 0, 3);
    check(frame.screen[0] == 1 && frame.colors[0] == 2,
          "runtime scripts copy their source before the caller can mutate it");
    script.tick(frame, 0, 3);
    check(frame.screen[1] == 2 && frame.colors[1] == 2,
          "runtime scripts consume copied bytes in order");
    script.tick(frame, 0, 3);
    check(!script.active() && frame.screen[2] == 0xAA,
          "runtime FF terminates before bytes after the terminator");

    script.start(commands, 3, 0, 0);
    script.tick(frame, 0, 3);
    check(frame.screen[3] == 0 && script.column() == 4,
          "starting another bounded script replaces the previous source");

    TextScript unterminated;
    const std::array<std::uint8_t, 2> no_terminator{0x41, 0x42};
    expectRuntimeError([&] { unterminated.start(no_terminator); },
                       "runtime scripts require a bounded FF terminator");
}

void testRuntimeFastBatch()
{
    std::vector<std::uint8_t> source(41, 0x41);
    source.back() = 0xFF;
    CharacterFrame frame;
    TextScript script;
    script.start(source, 1, 13, 0);
    script.tick(frame, 0, 0);
    check(script.active() && script.column() == 41 && script.row() == 13,
          "runtime cadence zero consumes 40 bytes and keeps a linear cursor");
    check(frame.screen[521] == 1 && frame.screen[560] == 1 && frame.screen[561] == 0,
          "runtime cadence zero writes exactly one 40-byte batch");
    script.tick(frame, 0, 0);
    check(!script.active(), "runtime cadence zero consumes its FF on the next batch");
}

void testBounds(std::span<const std::uint8_t> commands,
                std::span<const std::uint8_t> edge,
                std::span<const std::uint8_t> newline,
                std::span<const std::uint8_t> unterminated)
{
    TextScript script;
    CharacterFrame frame;
    expectRuntimeError([&] { script.start(std::span<const std::uint8_t>{}); }, "start rejects empty input");
    expectOutOfRange([&] { script.start(commands, 40); }, "start rejects a column outside screen");
    expectOutOfRange([&] { script.start(commands, 0, 25); }, "start rejects a row outside screen");
    script.start(commands);
    expectOutOfRange([&] { script.set_cursor(40, 0); }, "set_cursor rejects an invalid column");

    script.start(edge, 39, 24);
    script.tick(frame, 0, 3); // first A is still the bottom-right cell
    check(frame.screen[24 * kScreenColumns + 39] == 1,
          "the final in-range screen cell can be written");
    expectOutOfRange([&] { script.tick(frame, 0, 3); },
                     "a subsequent write beyond the screen is rejected");

    script.start(newline, 0, 24);
    expectOutOfRange([&] { script.tick(frame, 0, 3); },
                     "newline beyond the final row is rejected");

    expectRuntimeError([&] { script.start(unterminated); },
                       "bounded source without terminator is rejected before rendering");
}

void testRealScript(const Payload& payload)
{
    CharacterFrame frame;
    frame.screen.fill(0xAA);
    frame.colors.fill(0xBB);
    TextScript script;
    script.start(payload.asset("text/franchise_intro"));

    for (int index = 0; index < 204; ++index) {
        script.tick(frame, 0, 3);
    }
    check(script.active(), "the pinned account/name script remains active before its FF");
    script.tick(frame, 0, 3);
    check(!script.active() && script.row() == 12 && script.column() == 19,
          "the pinned script ends at the documented row 12, column 19");

    // Independent checkpoints from $AB13..$ABDF, including the initial spaces.
    check(frame.screen[1 * kScreenColumns + 8] == 0 &&
              frame.screen[1 * kScreenColumns + 12] == 7 &&
              frame.screen[1 * kScreenColumns + 23] == 19,
          "the opening account/name text has the expected C64 screen codes");
    check(frame.screen[3 * kScreenColumns + 5] == 6 &&
              frame.screen[10 * kScreenColumns + 11] == 7 &&
              frame.screen[12 * kScreenColumns + 18] == 0,
          "later script lines have the expected independent checkpoints");
    check(frame.colors[1 * kScreenColumns + 12] == 0 && frame.screen[0] == 0xAA &&
              frame.colors[0] == 0xBB,
          "script writes color zero and preserves untouched cells");
}

} // namespace

int main()
{

    try {
        constexpr std::array<std::uint8_t, 7> commands{
            0x20, 0x41, 0x00, 0x85, 0x0D, 0x42, 0xFF};
        constexpr std::array<std::uint8_t, 3> spaces{0x41, 0x20, 0xFF};
        constexpr std::array<std::uint8_t, 3> edge{0x41, 0x41, 0xFF};
        constexpr std::array<std::uint8_t, 2> newline{0x0D, 0xFF};
        constexpr std::array<std::uint8_t, 1> unterminated{0x41};
        std::array<std::uint8_t, 121> fast{};
        fast.fill(0x20);
        fast.back() = 0xFF;

        testCadenceAndCommands(commands);
        testSoundEvents(commands);
        testOnlySpacesAutowrap(spaces);
        testBounds(commands, edge, newline, unterminated);
        testFastBatch(fast);
        testRuntimeSource(commands);
        testRuntimeFastBatch();
        testRealScript(Payload::embedded());
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "text script tests passed\n";
    return 0;
}
