#include "game/city_status.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

using ghostbusters::game::AccountBalanceBytes;
using ghostbusters::game::CityStatusInput;
using ghostbusters::game::update_city_status;
using ghostbusters::video::CharacterFrame;

constexpr std::size_t kRowStart = 22U * 40U;
int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

CharacterFrame sentinel_frame()
{
    CharacterFrame frame;
    frame.screen.fill(0x55);
    frame.charset.fill(0xA5);
    frame.colors.fill(0x0B);
    frame.background = 0x1F;
    frame.multicolor1 = 0x2E;
    frame.multicolor2 = 0x3D;
    frame.multicolor = false;
    return frame;
}

void check_unchanged(const CharacterFrame& actual, const CharacterFrame& expected,
                     const char* context)
{
    check(actual.screen == expected.screen, std::string(context) + " preserves screen");
    check(actual.charset == expected.charset, std::string(context) + " preserves charset");
    check(actual.colors == expected.colors, std::string(context) + " preserves colors");
    check(actual.background == expected.background,
          std::string(context) + " preserves background");
    check(actual.multicolor1 == expected.multicolor1,
          std::string(context) + " preserves multicolor1");
    check(actual.multicolor2 == expected.multicolor2,
          std::string(context) + " preserves multicolor2");
    check(actual.multicolor == expected.multicolor,
          std::string(context) + " preserves multicolor mode");
}

void testStateAndBusyGates()
{
    const auto input_frame = sentinel_frame();
    const CityStatusInput input{0xA5, 0x0B, {0x00, 0x12, 0x34}};

    for (const auto state : std::array<std::uint8_t, 2>{0x11, 0x2A}) {
        const auto output = update_city_status(state, false, input, input_frame);
        check_unchanged(output, input_frame, "out-of-range state");
    }

    const auto busy_output = update_city_status(0x18, true, input, input_frame);
    check_unchanged(busy_output, input_frame, "busy notice scroller");
}

void testActiveBoundariesAndWrites()
{
    const auto input_frame = sentinel_frame();
    const CityStatusInput input{0xA5, 0x0B, {0x00, 0x12, 0x34}};

    for (const auto state : std::array<std::uint8_t, 2>{0x12, 0x29}) {
        const auto output = update_city_status(state, false, input, input_frame);

        // $729A's 18-byte L3A04 source after $7615 conversion.
        const std::array<std::uint8_t, 18> expected_label{
            0x03, 0x09, 0x14, 0x19, 0x27, 0x13, 0x00, 0x10, 0x0B,
            0x00, 0x05, 0x0E, 0x05, 0x12, 0x07, 0x19, 0x3A, 0x00,
        };
        for (std::size_t index = 0; index < expected_label.size(); ++index) {
            check(output.screen[kRowStart + 1 + index] == expected_label[index],
                  "active state writes CITY'S PK ENERGY label");
        }

        // $5B goes through $99B7 then $9832: a zero high nibble is returned
        // as a blank cell. $5A only goes through $99B7 and keeps its '0'+nibble.
        check(output.screen[kRowStart + 19] == 0x00,
              "$5B high zero is cleared by $9832");
        check(output.screen[kRowStart + 20] == 0x3B,
              "$5B low nibble is ASCII-like");
        check(output.screen[kRowStart + 21] == 0x3A,
              "$5A high nibble is ASCII-like");
        check(output.screen[kRowStart + 22] == 0x35,
              "$5A low nibble is ASCII-like");

        // $72AC-$72BB copies the seven-cell, pre-$9063 balance field.
        const std::array<std::uint8_t, 7> expected_balance{
            0x00, 0x00, 0x24, 0x31, 0x32, 0x33, 0x34,
        };
        for (std::size_t index = 0; index < expected_balance.size(); ++index) {
            check(output.screen[kRowStart + 30 + index] == expected_balance[index],
                  "active state writes seven-cell balance field");
        }

        // $5770, the gap at columns 23-29, and the final row cells are not
        // touched by this block. Other CharacterFrame planes are untouched.
        for (const auto column : std::array<std::size_t, 9>{0, 23, 24, 25, 26, 27, 28, 29, 37}) {
            check(output.screen[kRowStart + column] == 0x55,
                  "status updater preserves an unowned row cell");
        }
        check(output.charset == input_frame.charset,
              "active status preserves charset");
        check(output.colors == input_frame.colors,
              "active status preserves colors");
        check(output.background == input_frame.background &&
                  output.multicolor1 == input_frame.multicolor1 &&
                  output.multicolor2 == input_frame.multicolor2 &&
                  output.multicolor == input_frame.multicolor,
              "active status preserves CharacterFrame display properties");
        check(input_frame.screen[kRowStart + 1] == 0x55,
              "pure update leaves its input frame unchanged");
    }
}

void testStatusDigitsAndMoneyFormatting()
{
    auto input_frame = sentinel_frame();

    auto output = update_city_status(0x18, false,
                                     CityStatusInput{0x00, 0x00, {0x00, 0x00, 0x00}},
                                     input_frame);
    const std::array<std::uint8_t, 4> expected_zero_status{0x00, 0x30, 0x30, 0x30};
    for (std::size_t index = 0; index < expected_zero_status.size(); ++index) {
        check(output.screen[kRowStart + 19 + index] == expected_zero_status[index],
              "status helper preserves original zero-byte asymmetry");
    }
    const std::array<std::uint8_t, 7> expected_zero_money{
        0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x30,
    };
    for (std::size_t index = 0; index < expected_zero_money.size(); ++index) {
        check(output.screen[kRowStart + 30 + index] == expected_zero_money[index],
              "zero balance keeps the final zero and right-aligns it");
    }

    output = update_city_status(
        0x18, false,
        CityStatusInput{0x00, 0x00, AccountBalanceBytes{0xAF, 0xB2, 0xC3}},
        input_frame);
    const std::array<std::uint8_t, 7> expected_unusual_money{
        0x24, 0x3A, 0x3F, 0x3B, 0x32, 0x3C, 0x33,
    };
    for (std::size_t index = 0; index < expected_unusual_money.size(); ++index) {
        check(output.screen[kRowStart + 30 + index] == expected_unusual_money[index],
              "money nibbles retain 0x30-plus-nibble semantics");
    }
}

} // namespace

int main()
{
    testStateAndBusyGates();
    testActiveBoundariesAndWrites();
    testStatusDigitsAndMoneyFormatting();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "city status tests passed\n";
    return 0;
}
