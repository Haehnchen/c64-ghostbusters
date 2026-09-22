#include "video/character_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

using ghostbusters::video::CharacterFrame;
using ghostbusters::video::IndexedImage;
using ghostbusters::video::render_characters;

constexpr std::size_t kImageWidth = 320;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::uint8_t pixel(const IndexedImage& image, std::size_t x, std::size_t y)
{
    return image[y * kImageWidth + x];
}

void testMulticolorPairs()
{
    CharacterFrame frame;
    frame.background = 0x1F;
    frame.multicolor1 = 0x22;
    frame.multicolor2 = 0x3E;
    frame.screen[0] = 0;
    frame.colors[0] = 0x0B; // Multicolor flag plus cell color 3.
    frame.charset[0] = 0x1B; // 00, 01, 10, 11.

    const auto image = render_characters(frame);
    const std::uint8_t expected[] = {0x0F, 0x0F, 0x02, 0x02,
                                     0x0E, 0x0E, 0x03, 0x03};
    for (std::size_t x = 0; x < 8; ++x) {
        check(pixel(image, x, 0) == expected[x],
              "multicolor pairs map to doubled logical pixels");
    }
    check(pixel(image, 0, 1) == 0x0F, "multicolor renders each glyph row");
}

void testHiresCellAndGlobalMode()
{
    CharacterFrame frame;
    frame.background = 0x1F;
    frame.multicolor = true;
    frame.screen[1] = 1;
    frame.colors[1] = 0x06; // Bit 3 clear: this cell remains hires globally.
    frame.charset[8] = 0xA5; // 10100101.

    frame.screen[2] = 2;
    frame.colors[2] = 0x0E; // High foreground color must remain hires when global MC is off.
    frame.charset[16] = 0x80;

    const auto image = render_characters(frame);
    const std::uint8_t hiresExpected[] = {0x06, 0x0F, 0x06, 0x0F,
                                          0x0F, 0x06, 0x0F, 0x06};
    for (std::size_t x = 0; x < 8; ++x) {
        check(pixel(image, 8 + x, 0) == hiresExpected[x],
              "cell color bit 3 selects hires for a cell in global MC mode");
    }

    frame.multicolor = false;
    const auto globalHiresImage = render_characters(frame);
    check(pixel(globalHiresImage, 16, 0) == 0x0E,
          "global hires mode preserves a foreground color with bit 3 set");
    check(pixel(globalHiresImage, 17, 0) == 0x0F,
          "global hires mode uses the background for a zero glyph bit");
}

void testLastCellAndUnusedEntries()
{
    CharacterFrame frame;
    frame.background = 0x04;
    frame.multicolor = false;
    frame.screen[999] = 0xFF;
    frame.colors[999] = 0x0D;
    frame.charset[2047] = 0x01; // Last glyph row of the last character.

    const auto image = render_characters(frame);
    check(pixel(image, 319, 199) == 0x0D,
          "last cell uses the last glyph byte at the bottom-right pixel");
    check(pixel(image, 318, 199) == 0x04,
          "last cell renders the remaining glyph bits");

    CharacterFrame withUnusedEntries = frame;
    for (std::size_t index = 1000; index < withUnusedEntries.screen.size(); ++index) {
        withUnusedEntries.screen[index] = 0xAA;
        withUnusedEntries.colors[index] = 0xFF;
    }
    check(render_characters(withUnusedEntries) == image,
          "the unused final 24 screen and color entries do not affect rendering");
}

void testForegroundMaskIgnoresPaletteEquality()
{
    CharacterFrame frame;
    frame.background = frame.multicolor1 = frame.multicolor2 = 0;
    frame.colors[0] = 8;
    frame.charset[0] = 0x1B;
    IndexedImage mask{};
    const auto image = render_characters(frame, &mask);
    for (unsigned x = 0; x < 8; ++x) {
        check(image[x] == 0, "all multicolor codes may share the same visible color");
        check(mask[x] == (x >= 4), "only codes 10 and 11 obstruct sprites in multicolor mode");
    }
    frame.multicolor = false;
    frame.colors[0] = 0;
    frame.charset[0] = 0x80;
    (void)render_characters(frame, &mask);
    check(mask[0] == 1 && mask[1] == 0 && mask[4] == 0,
          "hires mask uses glyph bits and overwrites the previous frame mask");
}

} // namespace

int main()
{
    testMulticolorPairs();
    testHiresCellAndGlobalMode();
    testLastCellAndUnusedEntries();
    testForegroundMaskIgnoresPaletteEquality();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "character frame tests passed\n";
    return 0;
}
