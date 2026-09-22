#include "game/bcd_money.hpp"

#include <iostream>
#include <string>

namespace {

using ghostbusters::game::AccountBalanceBytes;
using ghostbusters::game::canAfford;
using ghostbusters::game::subtractMoney;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testVehiclePrices()
{
    const AccountBalanceBytes initial{0x01, 0x00, 0x00};
    check(canAfford(initial, {0x00, 0x20, 0x00}), "$10000 affords compact");
    check(subtractMoney(initial, {0x00, 0x20, 0x00}) ==
              AccountBalanceBytes{0x00, 0x80, 0x00},
          "$10000 minus $2000 is $8000");
    check(subtractMoney(initial, {0x00, 0x48, 0x00}) ==
              AccountBalanceBytes{0x00, 0x52, 0x00},
          "$10000 minus $4800 is $5200");
    check(subtractMoney(initial, {0x00, 0x60, 0x00}) ==
              AccountBalanceBytes{0x00, 0x40, 0x00},
          "$10000 minus $6000 is $4000");
    check(!canAfford(initial, {0x01, 0x50, 0x00}), "$10000 cannot afford $15000");
    check(subtractMoney(initial, {0x01, 0x50, 0x00}) == AccountBalanceBytes{},
          "underflow is clamped to three zero bytes");
}

void testCarryAcrossBytes()
{
    check(subtractMoney({0x12, 0x00, 0x00}, {0x00, 0x00, 0x01}) ==
              AccountBalanceBytes{0x11, 0x99, 0x99},
          "decimal borrow propagates from byte59 through byte57");
    check(canAfford({0x00, 0x00, 0x01}, {0x00, 0x00, 0x01}),
          "equal three-byte amounts retain carry");
    check(subtractMoney({0x00, 0x00, 0x01}, {0x00, 0x00, 0x01}) ==
              AccountBalanceBytes{},
          "equal amounts subtract to zero without underflow");
}

void testInvalidNibblesRemainHardwareInputs()
{
    check(canAfford({0x00, 0x00, 0xFA}, {0x00, 0x00, 0x01}),
          "binary carry still reports no borrow for an invalid low byte");
    check(subtractMoney({0x00, 0x00, 0xFA}, {0x00, 0x00, 0x01}) ==
              AccountBalanceBytes{0x00, 0x00, 0xF9},
          "invalid FA is passed through NMOS decimal correction, not normalized");
    check(subtractMoney({0x00, 0x01, 0x00}, {0x00, 0x00, 0x0A}) ==
              AccountBalanceBytes{0x00, 0x00, 0x90},
          "invalid price nibble preserves the original cross-byte borrow behavior");
    check(subtractMoney({0x00, 0x00, 0xFF}, {0x00, 0x00, 0x00}) ==
              AccountBalanceBytes{0x00, 0x00, 0xFF},
          "invalid FF remains FF when subtracting zero without a borrow");
    check(subtractMoney({0x00, 0x10, 0x00}, {0x00, 0x0F, 0x01}) ==
              AccountBalanceBytes{0x00, 0x0A, 0x99},
          "$10-$0f with an incoming borrow exercises the low-nibble -16 edge");
}

} // namespace

int main()
{
    testVehiclePrices();
    testCarryAcrossBytes();
    testInvalidNibblesRemainHardwareInputs();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "BCD money tests passed\n";
    return 0;
}
