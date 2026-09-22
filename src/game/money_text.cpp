#include "game/money_text.hpp"

#include <array>
#include <cstddef>

namespace ghostbusters::game {
namespace {

std::uint8_t ascii_nibble(std::uint8_t value) noexcept
{
    return static_cast<std::uint8_t>(0x30U + (value & 0x0FU));
}

} // namespace

std::array<std::uint8_t, 8> format_money_field(AccountBalanceBytes balance)
{
    // $9D24 first builds a leading '0' and six ASCII-like nibble characters.
    std::array<std::uint8_t, 8> raw{
        0x30,
        ascii_nibble(static_cast<std::uint8_t>(balance.byte57 >> 4U)),
        ascii_nibble(balance.byte57),
        ascii_nibble(static_cast<std::uint8_t>(balance.byte58 >> 4U)),
        ascii_nibble(balance.byte58),
        ascii_nibble(static_cast<std::uint8_t>(balance.byte59 >> 4U)),
        ascii_nibble(balance.byte59),
        0xFF,
    };

    // $9D74 turns leading zero characters into spaces. Its look-ahead keeps
    // the final zero in the all-zero case, and $9063 compacts leading spaces.
    std::size_t index = 0;
    while (raw[index + 1] != 0xFF && raw[index + 1] != 0x2E && raw[index] == 0x30) {
        raw[index] = 0x20;
        ++index;
    }
    raw[index == 0 ? 0 : index - 1] = 0x24;
    return raw;
}

std::vector<std::uint8_t> format_money(AccountBalanceBytes balance)
{
    const auto raw = format_money_field(balance);
    std::size_t first = 0;
    while (raw[first] == 0x20) ++first;
    return std::vector<std::uint8_t>(raw.begin() + static_cast<std::ptrdiff_t>(first), raw.end());
}

} // namespace ghostbusters::game
