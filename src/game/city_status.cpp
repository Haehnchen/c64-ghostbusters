#include "game/city_status.hpp"

#include "game/money_text.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace ghostbusters::game {
namespace {

constexpr std::uint8_t kFirstState = 0x12;
constexpr std::uint8_t kPastLastState = 0x2A;
constexpr std::size_t kRowStart = 22U * 40U;

// The source at $3A04 is the 18-byte status label used by $729A.  It is
// stored in the cartridge in the same ASCII-like form as the other game
// strings; $7615 maps spaces and uppercase bytes to screen codes.
constexpr std::array<std::uint8_t, 18> kStatusLabel{
    0x43, 0x49, 0x54, 0x59, 0x27, 0x53, 0x20, 0x50, 0x4B,
    0x20, 0x45, 0x4E, 0x45, 0x52, 0x47, 0x59, 0x3A, 0x20,
};

std::uint8_t screen_code(std::uint8_t value) noexcept
{
    // $7615: spaces become blank glyphs and values at or above $40 are
    // reduced by $40.  The SEC before SBC in the original makes this an
    // ordinary unsigned subtraction.
    if (value == 0x20) return 0;
    if (value >= 0x40) return static_cast<std::uint8_t>(value - 0x40);
    return value;
}

struct StatusDigits {
    std::uint8_t high;
    std::uint8_t low;
};

StatusDigits split_status_byte(std::uint8_t value,
                               bool clear_zero_high_digit) noexcept
{
    // $99B7 returns $23 = '0' + high nibble and $24 = '0' + low
    // nibble.  $9832 is called only for $5B and clears $23 when it is
    // exactly '0'; its accumulator then returns zero to the caller.
    auto high = static_cast<std::uint8_t>(0x30U + (value >> 4U));
    const auto low = static_cast<std::uint8_t>(0x30U + (value & 0x0FU));
    if (clear_zero_high_digit && high == 0x30) high = 0;
    return StatusDigits{high, low};
}

std::array<std::uint8_t, 7> format_balance_field(AccountBalanceBytes balance)
{
    // The status row uses the formatter's seven visible workspace bytes,
    // mapped to screen codes without compacting their leading spaces.
    const auto raw = format_money_field(balance);
    std::array<std::uint8_t, 7> field{};
    for (std::size_t index = 0; index < field.size(); ++index) {
        field[index] = screen_code(raw[index]);
    }
    return field;
}

} // namespace

video::CharacterFrame update_city_status(std::uint8_t state, bool notice_busy,
                                         CityStatusInput input,
                                         const video::CharacterFrame& frame)
{
    auto result = frame;
    if (state < kFirstState || state >= kPastLastState || notice_busy) {
        return result;
    }

    // $727B-$7295: status bytes are intentionally independent inputs.  The
    // original calls $9832 for $5B only, so a zero high nibble there becomes
    // a blank screen cell while $5A keeps its ASCII '0'.
    const auto status_b = split_status_byte(input.status5b, true);
    const auto status_a = split_status_byte(input.status5a, false);
    result.screen[kRowStart + 19] = status_b.high;
    result.screen[kRowStart + 20] = status_b.low;
    result.screen[kRowStart + 21] = status_a.high;
    result.screen[kRowStart + 22] = status_a.low;

    // $729A-$72A4: X counts down, but the indexed source and destination
    // share X, preserving the cartridge label's forward order.
    for (std::size_t index = 0; index < kStatusLabel.size(); ++index) {
        result.screen[kRowStart + 1 + index] = screen_code(kStatusLabel[index]);
    }

    // $72AC-$72BB: copy the seven visible bytes of EA14..EA1A.  The
    // terminator in the scratch buffer is outside this row field.
    const auto balance_field = format_balance_field(input.balance);
    for (std::size_t index = 0; index < balance_field.size(); ++index) {
        result.screen[kRowStart + 30 + index] = balance_field[index];
    }

    return result;
}

} // namespace ghostbusters::game
