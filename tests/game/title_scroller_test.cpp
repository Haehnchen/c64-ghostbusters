#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/title_scroller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::TitleScroller;
using ghostbusters::video::CharacterFrame;

constexpr std::uint16_t kPhaseMask = 0x03FF;
constexpr std::size_t kTextLength = 128;
constexpr std::size_t kRowStart = 960;
constexpr std::size_t kRowLength = 40;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::uint8_t expectedScreenCode(std::uint8_t value)
{
    if (value == 0x20) return 0;
    if (value >= 0x40) return static_cast<std::uint8_t>(value & 0x3F);
    return value;
}

void testPhaseOracle()
{
    for (std::uint16_t phase = 0; phase <= kPhaseMask; ++phase) {
        TitleScroller scroller;
        scroller.phase = phase;
        check(scroller.fine_scroll() == (phase & 0x07),
              "fine_scroll exposes the phase low three bits");
        check(scroller.source_index() == static_cast<std::uint8_t>((~(phase >> 3)) & 0x7F),
              "source_index follows the 128-byte reverse phase mapping");

        for (const std::uint8_t control : {0x00, 0x40, 0x80, 0xC0}) {
            scroller.phase = phase;
            scroller.tick(control);
            const auto expected = control == 0x80
                                      ? phase
                                      : static_cast<std::uint16_t>((phase - 1) & kPhaseMask);
            check(scroller.phase == expected,
                  "tick applies the control-bit hold/decrement rule");
        }
    }

    TitleScroller first;
    first.tick();
    check(first.phase == 1023 && first.source_index() == 0,
          "the first decrement wraps to phase 1023 and source index 0");
    for (int tick = 1; tick < 9; ++tick) first.tick();
    check(first.source_index() == 1,
          "the source index advances after nine decrements");

    const auto heldPhase = first.phase;
    first.tick(0x80);
    check(first.phase == heldPhase, "control $80 holds the phase");
}

void testScrollerRow(const Payload& payload)
{
    CharacterFrame frame;
    for (std::size_t index = 0; index < frame.screen.size(); ++index) {
        frame.screen[index] = static_cast<std::uint8_t>((index * 13 + 7) & 0xFF);
        frame.colors[index] = static_cast<std::uint8_t>((index * 5 + 3) & 0xFF);
    }
    frame.charset.fill(0xA5);
    const auto beforeScreen = frame.screen;
    const auto beforeColors = frame.colors;
    const auto beforeCharset = frame.charset;

    TitleScroller scroller;
    scroller.phase = 0; // source index 127: the first copied byte wraps to index 0.
    scroller.write_row(payload, frame);

    const auto source = payload.asset("ui/scroller");
    for (std::size_t index = 0; index < kRowStart; ++index) {
        check(frame.screen[index] == beforeScreen[index],
              "scroller leaves the first 960 screen cells unchanged");
    }
    for (std::size_t column = 0; column < kRowLength; ++column) {
        const auto sourceOffset = (127 + column) & (kTextLength - 1);
        check(frame.screen[kRowStart + column] == expectedScreenCode(source[sourceOffset]),
              "scroller copies a converted circular 40-byte source window");
    }
    for (std::size_t index = kRowStart + kRowLength; index < frame.screen.size(); ++index) {
        check(frame.screen[index] == beforeScreen[index],
              "scroller leaves screen-memory tail after the visible row unchanged");
    }
    check(frame.colors == beforeColors, "scroller leaves all title colors unchanged");
    check(frame.charset == beforeCharset, "scroller leaves the character set unchanged");

    // The real title text exercises both PETSCII spaces and uppercase mapping.
    scroller.phase = static_cast<std::uint16_t>((~38u & 0x7Fu) << 3);
    scroller.write_row(payload, frame);
    const std::array<std::uint8_t, 6> pressPrefix{0x10, 0x12, 0x05, 0x13, 0x13, 0x00};
    for (std::size_t column = 0; column < pressPrefix.size(); ++column) {
        check(frame.screen[kRowStart + column] == pressPrefix[column],
              "uppercase and space source bytes map to C64 screen codes");
    }
}

} // namespace

int main()
{

    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        testPhaseOracle();
        testScrollerRow(payload);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "title scroller tests passed\n";
    return 0;
}
