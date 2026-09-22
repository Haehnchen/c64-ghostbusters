#include "game/account_codec.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>

namespace {

using ghostbusters::game::AccountBalanceBytes;
using ghostbusters::game::PackedAccount;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::array<std::uint8_t, 20> savedName(const std::string& visible)
{
    std::array<std::uint8_t, 20> result{};
    for (std::size_t index = 0; index < visible.size() && index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>(visible[index]);
    }
    return result;
}

void testKnownLfsrAndAccounts()
{
    using ghostbusters::game::advanceAccountLfsr;
    using ghostbusters::game::decodeAccount;
    using ghostbusters::game::encodeAccount;

    check(advanceAccountLfsr(0x01) == 0x02, "LFSR shifts a low one");
    check(advanceAccountLfsr(0x80) == 0x01, "LFSR feeds old bit 7 back");
    check(advanceAccountLfsr(0xFF) == 0xFE, "LFSR uses even parity for all taps");

    const auto doe = savedName("DOE,JOHN");
    const auto smith = savedName("SMITH,JOHN");
    check(encodeAccount(doe, {0x01, 0x00, 0x00}) == PackedAccount{0x00, 0x50, 0x20, 0x00},
          "DOE,JOHN $10000 matches the statically derived original vector");
    check(encodeAccount(smith, {0x01, 0x00, 0x00}) ==
              PackedAccount{0x00, 0x14, 0x21, 0x00},
          "SMITH,JOHN $10000 matches the statically derived original vector");

    const auto decoded = decodeAccount(doe, PackedAccount{0x00, 0x50, 0x20, 0x00});
    check(decoded.checksum_valid && decoded.usable,
          "known generated account passes checksum and caller usability checks");
    check(decoded.balance == AccountBalanceBytes{0x01, 0x00, 0x00},
          "known generated account restores the three balance bytes");
}

void testRoundTrips()
{
    using ghostbusters::game::decodeAccount;
    using ghostbusters::game::encodeAccount;

    auto name = savedName("VENKMAN,PETER");
    const std::array<AccountBalanceBytes, 5> balances{{
        {0x01, 0x00, 0x00}, {0x12, 0x34, 0x00}, {0x99, 0x99, 0x00},
        {0xFF, 0x01, 0x00}, {0x80, 0x00, 0x7E},
    }};
    for (const auto balance : balances) {
        const auto decoded = decodeAccount(name, encodeAccount(name, balance));
        check(decoded.checksum_valid && decoded.usable, "encoded state decodes as usable");
        check(decoded.balance.byte57 == balance.byte57 &&
                  decoded.balance.byte58 == balance.byte58 && decoded.balance.byte59 == 0,
              "round trip restores bytes 57/58 and forces unencoded byte 59 to zero");
    }
}

void testAliasesAndInvalidResults()
{
    using ghostbusters::game::decodeAccount;
    using ghostbusters::game::encodeAccount;

    const auto name = savedName("DOE,JOHN");
    const auto canonical = encodeAccount(name, {0x01, 0x00, 0x00});
    auto aliases = canonical;
    for (auto& byte : aliases) byte = static_cast<std::uint8_t>(byte | 0x88U);
    const auto aliasResult = decodeAccount(name, aliases);
    check(aliasResult.checksum_valid && aliasResult.usable &&
              aliasResult.balance == AccountBalanceBytes{0x01, 0x00, 0x00},
          "bit 3 of every input nibble is ignored exactly like the original");

    auto wrongName = name;
    wrongName[0] = 'X';
    const auto invalid = decodeAccount(wrongName, canonical);
    check(!invalid.checksum_valid && !invalid.usable,
          "same account is rejected for a different saved-name checksum");
    check(invalid.balance == AccountBalanceBytes{},
          "checksum failure returns the zeroed balance produced by $9155");

    std::array<std::uint8_t, 20> zeroName{};
    const auto zeroAccount = encodeAccount(zeroName, {});
    const auto zeroResult = decodeAccount(zeroName, zeroAccount);
    check(zeroResult.checksum_valid && !zeroResult.usable,
          "checksum validity is separate from the caller's nonzero-state rule");
}

void testAllTwentyNameBytes()
{
    using ghostbusters::game::accountNameSum;
    using ghostbusters::game::decodeAccount;
    using ghostbusters::game::encodeAccount;

    auto withTail = savedName("RAY");
    withTail[3] = 0;
    withTail[7] = 0x20;
    const auto withoutTail = savedName("RAY");
    check(accountNameSum(withTail) == static_cast<std::uint8_t>(accountNameSum(withoutTail) + 0x20),
          "name checksum includes Delete residue after the null terminator");

    const auto packed = encodeAccount(withTail, {0x01, 0x00, 0x00});
    check(decodeAccount(withTail, packed).usable, "tail-bound account decodes with identical tail");
    check(!decodeAccount(withoutTail, packed).checksum_valid,
          "removing a post-terminator tail byte changes account validity");
}

} // namespace

int main()
{
    testKnownLfsrAndAccounts();
    testRoundTrips();
    testAliasesAndInvalidResults();
    testAllTwentyNameBytes();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "account codec tests passed\n";
    return 0;
}
