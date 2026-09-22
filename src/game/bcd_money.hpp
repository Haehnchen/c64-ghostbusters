#pragma once

#include "game/account_codec.hpp"

namespace ghostbusters::game {

// Mirrors $9654: three NMOS-6510 decimal SBC operations from byte $59 up to
// byte $57. Inputs are kept as raw bytes; invalid BCD nibbles are intentional.
[[nodiscard]] bool canAfford(AccountBalanceBytes balance,
                             AccountBalanceBytes price) noexcept;

// Mirrors $9636. An underflow returns all-zero bytes, matching the original
// routine even though the normal caller checks canAfford() first.
[[nodiscard]] AccountBalanceBytes subtractMoney(AccountBalanceBytes balance,
                                                 AccountBalanceBytes price) noexcept;

} // namespace ghostbusters::game
