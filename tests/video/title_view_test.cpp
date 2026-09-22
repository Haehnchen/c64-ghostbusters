#include "game/pal_clock.hpp"
#include "video/title_view.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
}

int main()
{
    try {
        using namespace std::chrono_literals;
        ghostbusters::game::PalClock a, b;
        unsigned ticks_a = 0, ticks_b = 0;
        for (unsigned i = 0; i < 1000; ++i) ticks_a += a.advance(1ms);
        for (unsigned i = 0; i < 10; ++i) ticks_b += b.advance(100ms);
        check(ticks_a == 50 && ticks_a == ticks_b, "PAL clock depends on host update chunks");
        check(a.advance(-1ms) == 0, "Negative elapsed time advanced the clock");
        check(a.advance(10s) <= 13, "Suspend caused unbounded catch-up");

        ghostbusters::video::CharacterFrame frame;
        frame.colors.fill(1);
        frame.screen.fill(1);
        for (unsigned i = 8; i < 16; ++i) frame.charset[i] = 0x80;
        for (unsigned shift = 0; shift < 8; ++shift) {
            const auto image = ghostbusters::video::render_title(frame, shift);
            check(image[0] == 1, "Bottom scroll changed the main title layer");
            for (unsigned x = 0; x < 320; ++x) {
                const auto expected = x >= 8 && x < 312 && ((x - shift) % 8 == 0) ? 1 : 0;
                check(image[192 * 320 + x] == expected, "Bottom row clipping/scroll mismatch");
            }
        }
        // The lyric window moves without shifting the title logo or footer.
        // A row transfer plus phase wrap must still move only one pixel.
        frame.screen.fill(0);
        frame.charset.fill(0);
        frame.charset[8] = 0xFF;
        frame.charset[11] = 0x81;
        frame.screen[840 + 7] = 1;
        const auto stationary = ghostbusters::video::render_title(frame, 3);
        for (unsigned phase = 0; phase < 8; ++phase) {
            const auto moving = ghostbusters::video::render_title(frame, 3, phase);
            const auto repeated = ghostbusters::video::render_title(frame, 3, phase + 16);
            check(moving == repeated, "Lyric gate count changed its fine phase");
            for (unsigned y = 0; y < 200; ++y) {
                for (unsigned x = 0; x < 320; ++x) {
                    const unsigned source_y = y >= 152 && y < 192 ? y - phase : y;
                    check(moving[y * 320 + x] == stationary[source_y * 320 + x],
                          "Lyric fine phase moved the wrong pixels");
                }
            }
        }
        frame.screen[840 + 7] = 0;
        frame.screen[800 + 7] = 1;
        const auto wrapped = ghostbusters::video::render_title(frame, 3, 7);
        for (unsigned y = 152; y < 191; ++y) {
            for (unsigned x = 0; x < 320; ++x)
                check(wrapped[y * 320 + x] == stationary[(y + 1) * 320 + x],
                      "Character row transfer caused a visible eight-pixel jump");
        }

        std::array<std::uint8_t, 24> rainbow;
        rainbow.fill(255);
        const std::array<std::uint8_t, 8> colors{8,8,7,7,5,5,6,0};
        frame.screen.fill(0);
        frame.charset.fill(0);
        // Frozen gameplay phase places the rainbow at viewport X48.
        // It stays behind text and never changes the playfield above the strip.
        for (unsigned phase = 0; phase < 1024; ++phase) {
            ghostbusters::video::IndexedImage image;
            image.fill(9);
            ghostbusters::video::composite_footer(image, frame, phase, rainbow, colors);
            check(image[191 * 320 + 48] == 9, "Footer modified the playfield");
            for (unsigned y = 192; y < 200; ++y) {
                check(image[y * 320 + 6] == 0 && image[y * 320 + 311] == 0,
                      "Footer crossed its border");
            }
            const unsigned position = (phase - 952U) & 1023U;
            for (unsigned x = 7; x < 311; ++x) {
                const bool visible = position < 512 && x + 24 >= position && x < position;
                check(image[193 * 320 + x] == (visible ? 2 : 0),
                      "Rainbow position, clipping or wrap is wrong");
            }
        }
        ghostbusters::video::IndexedImage footer{};
        frame.screen[960 + 6] = 1;
        frame.charset[8] = 0x80;
        ghostbusters::video::composite_footer(footer, frame, 0, rainbow, colors);
        check(footer[193 * 320 + 48] == 1 && footer[193 * 320 + 49] == 2,
              "Rainbow must remain behind white credits");
        check(footer[194 * 320 + 48] == 8 && footer[196 * 320 + 48] == 7 &&
              footer[198 * 320 + 48] == 5, "Rainbow row colors are missing");
        const auto visible_footer = footer;
        const auto preserved_screen = frame.screen;
        footer[191 * 320 + 48] = 9;
        ghostbusters::video::composite_footer(footer, frame, 0, rainbow, colors, false);
        check(footer[191 * 320 + 48] == 9, "Speech footer blanking touched the playfield");
        for (unsigned pixel = 192 * 320; pixel < footer.size(); ++pixel)
            check(footer[pixel] == 0, "Speech left visible credits or rainbow pixels");
        check(frame.screen == preserved_screen, "Temporary blanking destroyed footer text");
        ghostbusters::video::composite_footer(footer, frame, 0, rainbow, colors);
        for (unsigned pixel = 192 * 320; pixel < footer.size(); ++pixel)
            check(footer[pixel] == visible_footer[pixel], "Footer failed to return after speech");
        std::cout << "title view and PAL clock tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
