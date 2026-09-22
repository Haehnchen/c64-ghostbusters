#include "game/account_number.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using Bytes = std::array<std::uint8_t, 4>;

void testEmptyAndShortInputs()
{
    const std::array<std::uint8_t, 0> empty{};
    check(ghostbusters::game::pack_account_number(empty) == Bytes{0, 0, 0, 0},
          "empty input leaves the zero-initialized accumulator unchanged");

    const std::array<std::uint8_t, 3> short_input{0x01, 0x02, 0x03};
    check(ghostbusters::game::pack_account_number(short_input) == Bytes{0, 0, 0x01, 0x23},
          "short input is packed in original order with leading zero bytes");
}

void testEightAndMoreInputs()
{
    const std::array<std::uint8_t, 8> eight{1, 2, 3, 4, 5, 6, 7, 8};
    check(ghostbusters::game::pack_account_number(eight) == Bytes{0x12, 0x34, 0x56, 0x78},
          "eight nibbles fill the four-byte accumulator");

    const std::array<std::uint8_t, 10> more{1, 2, 3, 4, 5, 6, 7, 8, 9, 0x0A};
    check(ghostbusters::game::pack_account_number(more) == Bytes{0x12, 0x34, 0x56, 0x78},
          "bytes after the eighth input are ignored");
}

void testNulAndNonNumericInputs()
{
    const std::array<std::uint8_t, 4> nul_terminated{0x01, 0x0A, 0x00, 0x0B};
    check(ghostbusters::game::pack_account_number(nul_terminated) == Bytes{0, 0, 0, 0x1A},
          "the first NUL stops input consumption");

    const std::array<std::uint8_t, 3> non_numeric{
        static_cast<std::uint8_t>('Y'), static_cast<std::uint8_t>('N'), 0xFF};
    check(ghostbusters::game::pack_account_number(non_numeric) == Bytes{0, 0, 0x09, 0xEF},
          "non-numeric bytes are accepted and reduced to low nibbles");
}

} // namespace

int main()
{
    testEmptyAndShortInputs();
    testEightAndMoreInputs();
    testNulAndNonNumericInputs();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "account number tests passed\n";
    return 0;
}
