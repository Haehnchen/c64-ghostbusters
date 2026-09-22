#include "assets/payload.hpp"
#include "assets/embedded.hpp"
#include "game/title_screen.hpp"
#include "game/title_sequence.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using ghostbusters::game::TitleSequence;
using ghostbusters::game::TitleSequenceEvent;

void require(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void test_timeline_shift_and_text(const ghostbusters::assets::Payload& payload)
{
    auto title = ghostbusters::game::prepare_title_screen(
        payload, ghostbusters::assets::embedded_font());
    auto& frame = title.characters;
    TitleSequence sequence(payload);

    for (std::size_t index = 0; index < 120; ++index)
        frame.screen[800 + index] = static_cast<std::uint8_t>(index + 1);
    const auto source = frame.screen;

    require(sequence.tick(frame, false) == TitleSequenceEvent::none,
            "timeline marker must not itself request speech");
    require(sequence.counter() == 2 && sequence.timeline_index() == 1 &&
                sequence.gate() == 15,
            "first PAL tick must reach marker 2 before decrementing the gate");
    for (std::size_t index = 0; index < 80; ++index)
        require(frame.screen[800 + index] == source[840 + index],
                "gate low bits 7 must shift the two source rows forward");
    for (std::size_t index = 0; index < 40; ++index)
        require(frame.screen[880 + index] == 0,
                "third title shift loop must clear the final row");

    for (unsigned tick = 0; tick < 15; ++tick)
        require(sequence.tick(frame, false) == TitleSequenceEvent::none,
                "ordinary lyric countdown must not request speech");
    require(sequence.gate() == 0 && sequence.text_cursor() == 2,
            "gate transition to zero must consume the initial space/CR line");
    for (std::size_t column = 7; column < 40; ++column)
        require(frame.screen[880 + column] == 0,
                "initial lyric line must blank columns 7 through 39");

    sequence.reset_timeline();
    require(sequence.counter() == 0 && sequence.timeline_index() == 0 &&
                sequence.gate() == 0 && sequence.text_cursor() == 2,
            "startup reset must affect only counter and timeline index");
}

void test_pending_space(const ghostbusters::assets::Payload& payload)
{
    ghostbusters::video::CharacterFrame frame;
    TitleSequence sequence(payload);

    require(sequence.tick(frame, true) == TitleSequenceEvent::none &&
                sequence.gate() == 15 && sequence.pending_space(),
            "Space edge during a gate must remain pending");
    require(sequence.tick(frame, false) == TitleSequenceEvent::none,
            "releasing a blocked Space must retain its pending event");

    TitleSequenceEvent result = TitleSequenceEvent::none;
    while (sequence.gate() != 0) result = sequence.tick(frame, false);
    require(result == TitleSequenceEvent::start_speech_1 &&
                !sequence.pending_space(),
            "pending Space must fire on the tick that decrements gate 1 to zero");

    require(sequence.tick(frame, true) == TitleSequenceEvent::start_speech_1,
            "a fresh Space edge at gate zero must request command 1");
    require(sequence.tick(frame, true) == TitleSequenceEvent::none,
            "held Space must not create another edge");
}

void test_speech_mode(const ghostbusters::assets::Payload& payload)
{
    ghostbusters::video::CharacterFrame frame;
    frame.screen.fill(0xA5);
    TitleSequence sequence(payload);

    require(sequence.tick(frame, true, true) == TitleSequenceEvent::none,
            "title speech must suspend Space polling");
    require(sequence.tick(frame, true, true) == TitleSequenceEvent::none,
            "title speech must not emit foreground requests");
    require(sequence.counter() == 2 && sequence.timeline_index() == 1 &&
                sequence.gate() == 16 && !sequence.pending_space(),
            "speech mode must advance timeline without decrementing its gate");
    for (const auto value : frame.screen)
        require(value == 0xA5,
                "speech mode must suspend title row and lyric writes");

    require(sequence.tick(frame, true) == TitleSequenceEvent::none &&
                sequence.gate() == 15 && sequence.pending_space(),
            "first foreground tick after speech must observe a fresh Space edge");
}

void test_text_end_wrap(const ghostbusters::assets::Payload& payload)
{
    ghostbusters::video::CharacterFrame frame;
    TitleSequence sequence(payload);

    unsigned ticks = 0;
    while ((sequence.timeline_index() != 0x70 || sequence.gate() != 0) &&
           ticks++ < 30000) {
        (void)sequence.tick(frame, false);
    }
    require(sequence.timeline_index() == 0x70 && sequence.gate() == 0,
            "complete title timeline must settle within its bounded PAL run");
    require(sequence.text_cursor() == 0x4AC,
            "53 timeline gates must leave the lyric cursor on its final FF");

    sequence.reset_timeline();
    while (sequence.gate() != 1 && ticks++ < 31000)
        (void)sequence.tick(frame, false);
    require(sequence.gate() == 1,
            "restarted timeline must reach the next lyric boundary");

    frame.screen.fill(0xA5);
    const auto before = frame.screen;
    require(sequence.tick(frame, false) == TitleSequenceEvent::none &&
                sequence.text_cursor() == 0,
            "FF lyric marker must wrap the cursor without an event");
    require(frame.screen == before,
            "FF lyric marker must not write a screen cell");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_timeline_shift_and_text(payload);
        test_pending_space(payload);
        test_speech_mode(payload);
        test_text_end_wrap(payload);
        std::cout << "title sequence tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
