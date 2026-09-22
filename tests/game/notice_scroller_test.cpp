#include "game/notice_scroller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using ghostbusters::game::NoticeScroller;
using ghostbusters::video::CharacterFrame;

constexpr std::size_t kRowStart = 880;
int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

unsigned tick_until(NoticeScroller& scroller, CharacterFrame& frame,
                    std::uint8_t state, std::uint8_t position,
                    unsigned maximum = 2048)
{
    unsigned tick = 0;
    for (; tick < maximum && scroller.position4b() != position; ++tick) {
        scroller.tick(state, frame);
    }
    check(scroller.position4b() == position, "scroller reached requested position");
    return tick;
}

void testQueueBoundsAndIdleGate()
{
    NoticeScroller scroller;
    const auto initial = scroller.snapshot();
    check(initial.phase49 == 0 && initial.length4a == 0 && initial.position4b == 0 &&
              initial.cached_status4c == 0,
          "new scroller has cleared control fields");
    check(initial.buffer.front() == 0xFF && initial.buffer.back() == 0xFF,
          "new E900 image is filled with terminators");

    check(!scroller.queue(std::span<const std::uint8_t>{}),
          "empty notice is rejected");
    const std::array<std::uint8_t, 2> with_terminator{0x41, 0xFF};
    check(!scroller.queue(with_terminator), "input terminator is rejected");

    std::array<std::uint8_t, NoticeScroller::max_payload_size + 1> oversized{};
    check(!scroller.queue(oversized), "notice exceeding the 255-byte payload is rejected");

    const std::array<std::uint8_t, 2> notice{0x41, 0x42};
    scroller.set_phase49(0xA5);
    check(scroller.queue(notice), "translated notice enters an idle scroller");
    check(scroller.phase49() == 0xA5 && scroller.length4a() == 2 &&
              scroller.position4b() == 1 && scroller.buffer()[0] == 0x41 &&
              scroller.buffer()[1] == 0x42 && scroller.buffer()[2] == 0xFF,
          "queue preserves phase and writes payload plus one terminator");
    check(!scroller.queue(notice), "busy scroller refuses a second notice");
    auto residual = initial;
    residual.length4a = 0xFF;
    NoticeScroller counter_wrap(residual);
    check(counter_wrap.queue(notice) && counter_wrap.length4a() == 1,
          "original INC4A preserves and wraps an idle residual length");
}

void testStateGateAndVisibleRow()
{
    NoticeScroller scroller;
    const std::array<std::uint8_t, 2> notice{0x41, 0x42};
    check(scroller.queue(notice), "state-gate notice queues");

    CharacterFrame frame;
    frame.screen.fill(0x5A);
    frame.colors.fill(0x0C);
    const auto before = frame;
    scroller.tick(0x0F, frame);
    check(scroller.phase49() == 0 && scroller.position4b() == 1 &&
              scroller.length4a() == 2,
          "shop state leaves queued controls untouched");
    check(frame.screen == before.screen && frame.colors == before.colors,
          "shop state does not draw the queued notice");

    frame.screen.fill(0);
    scroller.tick(0x12, frame);
    check(scroller.phase49() == 0xFE && scroller.position4b() == 2,
          "state 18 performs the two-byte phase step and first advance");
    check(frame.screen[kRowStart + 38] == 0x41 && frame.screen[kRowStart + 39] == 0x42,
          "the message enters from the right during its 40-byte prefix");

    // Advance to position 40. The first two data bytes then occupy columns 0
    // and 1; all cells after the terminator are blank.
    tick_until(scroller, frame, 0x12, 40);
    check(frame.screen[kRowStart] == 0x41 && frame.screen[kRowStart + 1] == 0x42 &&
              frame.screen[kRowStart + 2] == 0,
          "row 22 maps E900 bytes after its 40-cell prefix");
    check(frame.colors[kRowStart] == 0x0C && frame.colors[kRowStart + 1] == 0x0C,
          "notice drawing leaves color RAM untouched");
}

void testFinePhaseAndFullMessageExit()
{
    NoticeScroller scroller;
    const std::array<std::uint8_t, 1> notice{0x37};
    scroller.set_phase49(0x01);
    check(scroller.queue(notice), "fine-phase notice queues");
    check(scroller.phase49() == 0x01, "queue does not alter fine phase");

    CharacterFrame frame;
    const auto ticks_to_data = tick_until(scroller, frame, 0x12, 40);
    check(scroller.phase49() ==
              static_cast<std::uint8_t>(0x01 - 2 * ticks_to_data),
          "fine phase decrements by two per active tick");
    check(frame.screen[kRowStart] == 0x37 && frame.screen[kRowStart + 1] == 0,
          "single-byte message is visible at the first data position");

    // Position 41 sees the one-byte message's terminator before copying. The
    // final frame therefore remains the one written at position 40.
    const auto last_visible = frame.screen;
    tick_until(scroller, frame, 0x12, 0);
    check(scroller.length4a() == 0 && scroller.cached_status4c() == 0,
          "terminator exits the full message and clears length/cache");
    check(frame.screen == last_visible,
          "terminator exit leaves the last rendered row in place");
    check(scroller.queue(notice), "idle scroller accepts a new notice after exit");
}

void testLongMessageCompaction()
{
    std::array<std::uint8_t, 100> notice{};
    for (std::size_t index = 0; index < notice.size(); ++index) {
        notice[index] = static_cast<std::uint8_t>(index + 1);
    }

    NoticeScroller scroller;
    check(scroller.queue(notice), "long notice queues");
    CharacterFrame frame;
    for (unsigned tick = 0; tick < 2048 && scroller.length4a() != 60; ++tick) {
        scroller.tick(0x12, frame);
    }
    check(scroller.length4a() == 60, "long notice reached its compaction point");
    check(scroller.length4a() == 60 && scroller.position4b() == 80,
          "position 120 compacts and subtracts the 40-byte prefix");
    check(scroller.buffer()[0] == 41 && scroller.buffer()[39] == 80 &&
              scroller.buffer()[40] == 81 && scroller.buffer()[59] == 100 &&
              scroller.buffer()[60] == 0xFF,
          "compaction copies the tail including its terminator");
    check(frame.screen[kRowStart] == 81 && frame.screen[kRowStart + 1] == 82,
          "post-compaction row starts at the compacted tail window");
}

} // namespace

int main()
{
    testQueueBoundsAndIdleGate();
    testStateGateAndVisibleRow();
    testFinePhaseAndFullMessageExit();
    testLongMessageCompaction();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "notice scroller tests passed\n";
    return 0;
}
