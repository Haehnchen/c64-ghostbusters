#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace ghostbusters::game {

using PackedAccount = std::array<std::uint8_t, 4>;

struct AccountBalanceBytes {
    std::uint8_t byte57 = 0;
    std::uint8_t byte58 = 0;
    std::uint8_t byte59 = 0;

    [[nodiscard]] bool operator==(const AccountBalanceBytes&) const = default;
};

struct AccountDecodeResult {
    AccountBalanceBytes balance{};
    bool checksum_valid = false;
    bool usable = false;
};

// The original checksum covers all 20 saved bytes, including bytes after the
// first null terminator. A fixed-extent span makes accidental truncation at
// the visible name impossible.
[[nodiscard]] std::uint8_t accountNameSum(
    std::span<const std::uint8_t, 20> savedName) noexcept;

[[nodiscard]] std::uint8_t advanceAccountLfsr(std::uint8_t state) noexcept;

[[nodiscard]] AccountDecodeResult decodeAccount(
    std::span<const std::uint8_t, 20> savedName,
    const PackedAccount& packed) noexcept;

// Accounts retain only the two high balance bytes. The low balance byte is
// deliberately absent and is restored as zero by decodeAccount().
[[nodiscard]] PackedAccount encodeAccount(
    std::span<const std::uint8_t, 20> savedName,
    AccountBalanceBytes balance) noexcept;

} // namespace ghostbusters::game
