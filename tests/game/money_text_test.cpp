#include "game/money_text.hpp"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect(ghostbusters::game::AccountBalanceBytes balance,
            std::initializer_list<std::uint8_t> expected, const char* message)
{
    const auto actual = ghostbusters::game::format_money(balance);
    check(actual == std::vector<std::uint8_t>(expected), message);
}

void testZeroAndNormalBalances()
{
    expect({0x00, 0x00, 0x00}, {0x24, 0x30, 0xFF},
           "zero balance formats as $0 and keeps the terminator");
    expect({0x01, 0x00, 0x00}, {0x24, 0x31, 0x30, 0x30, 0x30, 0x30, 0xFF},
           "one thousand in the high balance byte formats as $10000");
    expect({0x00, 0x12, 0x34}, {0x24, 0x31, 0x32, 0x33, 0x34, 0xFF},
           "leading zero bytes are removed without decimal conversion");
}

void testUnusualNibbles()
{
    expect({0xAF, 0xB2, 0xC3},
           {0x24, 0x3A, 0x3F, 0x3B, 0x32, 0x3C, 0x33, 0xFF},
           "non-BCD nibbles retain the original 0x30-plus-nibble mapping");
    expect({0x00, 0x00, 0x0A}, {0x24, 0x3A, 0xFF},
           "a low nibble above nine is not treated as a decimal digit");
}

} // namespace

int main()
{
    testZeroAndNormalBalances();
    testUnusualNibbles();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "money text tests passed\n";
    return 0;
}
