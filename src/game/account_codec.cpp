#include "game/account_codec.hpp"

#include <cstddef>

namespace ghostbusters::game {
namespace {

std::uint8_t accountCheck(std::span<const std::uint8_t, 20> savedName,
                          std::uint8_t high, std::uint8_t low) noexcept
{
    auto state = static_cast<std::uint8_t>(high + low);
    if (state == 0) state = 1;

    const auto sum = accountNameSum(savedName);
    // A wrapped zero name sum performs a full 256-step checksum cycle.
    const unsigned iterations = sum == 0 ? 256U : static_cast<unsigned>(sum);
    for (unsigned index = 0; index < iterations; ++index) {
        state = advanceAccountLfsr(state);
    }
    return state;
}

} // namespace

std::uint8_t accountNameSum(std::span<const std::uint8_t, 20> savedName) noexcept
{
    std::uint8_t sum = 0;
    for (const auto byte : savedName) {
        sum = static_cast<std::uint8_t>(sum + byte);
    }
    return sum;
}

std::uint8_t advanceAccountLfsr(std::uint8_t state) noexcept
{
    auto feedbackBits = static_cast<std::uint8_t>(state & 0xB8U);
    feedbackBits ^= static_cast<std::uint8_t>(feedbackBits >> 4U);
    feedbackBits ^= static_cast<std::uint8_t>(feedbackBits >> 2U);
    feedbackBits ^= static_cast<std::uint8_t>(feedbackBits >> 1U);
    const auto feedback = static_cast<std::uint8_t>(feedbackBits & 1U);
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(state << 1U) | feedback);
}

AccountDecodeResult decodeAccount(std::span<const std::uint8_t, 20> savedName,
                                  const PackedAccount& packed) noexcept
{
    std::array<std::uint8_t, 8> digits{};
    // Only three bits per input nibble count: 8/9 intentionally alias 0/1.
    // Do not add decimal-digit validation or reject these saved accounts.
    for (std::size_t index = 0; index < packed.size(); ++index) {
        digits[index * 2] = static_cast<std::uint8_t>((packed[index] >> 4U) & 7U);
        digits[index * 2 + 1] = static_cast<std::uint8_t>(packed[index] & 7U);
    }

    const std::uint32_t payload =
        (static_cast<std::uint32_t>(digits[6]) << 21U) |
        (static_cast<std::uint32_t>(digits[7]) << 18U) |
        (static_cast<std::uint32_t>(digits[4]) << 15U) |
        (static_cast<std::uint32_t>(digits[5]) << 12U) |
        (static_cast<std::uint32_t>(digits[2]) << 9U) |
        (static_cast<std::uint32_t>(digits[3]) << 6U) |
        (static_cast<std::uint32_t>(digits[0]) << 3U) |
        static_cast<std::uint32_t>(digits[1]);

    const auto high = static_cast<std::uint8_t>(payload >> 16U);
    const auto check = static_cast<std::uint8_t>(payload >> 8U);
    const auto low = static_cast<std::uint8_t>(payload);
    const bool checksumValid = accountCheck(savedName, high, low) == check;

    AccountDecodeResult result;
    result.checksum_valid = checksumValid;
    if (checksumValid) {
        result.balance.byte57 = high;
        result.balance.byte58 = low;
    }
    result.usable = checksumValid && static_cast<std::uint8_t>(high | low) != 0;
    return result;
}

PackedAccount encodeAccount(std::span<const std::uint8_t, 20> savedName,
                            AccountBalanceBytes balance) noexcept
{
    const auto check = accountCheck(savedName, balance.byte57, balance.byte58);
    const std::uint32_t payload = (static_cast<std::uint32_t>(balance.byte57) << 16U) |
                                  (static_cast<std::uint32_t>(check) << 8U) |
                                  static_cast<std::uint32_t>(balance.byte58);
    const std::array<std::uint8_t, 8> digits{
        static_cast<std::uint8_t>((payload >> 3U) & 7U),
        static_cast<std::uint8_t>(payload & 7U),
        static_cast<std::uint8_t>((payload >> 9U) & 7U),
        static_cast<std::uint8_t>((payload >> 6U) & 7U),
        static_cast<std::uint8_t>((payload >> 15U) & 7U),
        static_cast<std::uint8_t>((payload >> 12U) & 7U),
        static_cast<std::uint8_t>((payload >> 21U) & 7U),
        static_cast<std::uint8_t>((payload >> 18U) & 7U),
    };

    PackedAccount packed{};
    for (std::size_t index = 0; index < packed.size(); ++index) {
        packed[index] = static_cast<std::uint8_t>((digits[index * 2] << 4U) |
                                                  digits[index * 2 + 1]);
    }
    return packed;
}

} // namespace ghostbusters::game
