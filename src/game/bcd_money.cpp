#include "game/bcd_money.hpp"

#include <cstdint>

namespace ghostbusters::game {
namespace {

struct SbcResult {
    std::uint8_t value;
    bool carry;
};

// NMOS 6502/6510 decimal SBC. Carry is the no-borrow result of the binary
// subtraction; decimal corrections affect the accumulator byte, including
// for inputs whose nibbles are greater than nine.
SbcResult decimalSbc(std::uint8_t accumulator, std::uint8_t operand,
                     bool carryIn) noexcept
{
    const int borrow = carryIn ? 0 : 1;
    const int binary = static_cast<int>(accumulator) -
                       static_cast<int>(operand) - borrow;

    // NMOS decimal correction operates on separate nibbles. Keeping the low
    // correction isolated is observable for invalid inputs: $10-$0f-1 has a
    // low difference of -16 and produces $0a, rather than a full-byte $fa.
    int low = static_cast<int>(accumulator & 0x0FU) -
              static_cast<int>(operand & 0x0FU) - borrow;
    const bool lowBorrow = low < 0;
    if (lowBorrow) {
        low -= 6;
    }
    int high = static_cast<int>(accumulator >> 4U) -
               static_cast<int>(operand >> 4U) - (lowBorrow ? 1 : 0);
    if (high < 0) {
        high -= 6;
    }
    const auto corrected = static_cast<std::uint8_t>(
        ((high & 0x0F) << 4) | (low & 0x0F));
    return {corrected, binary >= 0};
}

struct MoneySubtraction {
    AccountBalanceBytes balance;
    bool carry;
};

MoneySubtraction subtractRaw(AccountBalanceBytes balance,
                             AccountBalanceBytes price) noexcept
{
    auto part = decimalSbc(balance.byte59, price.byte59, true);
    balance.byte59 = part.value;
    part = decimalSbc(balance.byte58, price.byte58, part.carry);
    balance.byte58 = part.value;
    part = decimalSbc(balance.byte57, price.byte57, part.carry);
    balance.byte57 = part.value;
    return {balance, part.carry};
}

} // namespace

bool canAfford(AccountBalanceBytes balance, AccountBalanceBytes price) noexcept
{
    return subtractRaw(balance, price).carry;
}

AccountBalanceBytes subtractMoney(AccountBalanceBytes balance,
                                  AccountBalanceBytes price) noexcept
{
    const auto result = subtractRaw(balance, price);
    return result.carry ? result.balance : AccountBalanceBytes{};
}

} // namespace ghostbusters::game
