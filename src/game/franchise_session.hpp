#pragma once

#include "game/account_codec.hpp"

#include <array>
#include <cstdint>

namespace ghostbusters::game {

// State retained by the title/franchise flow for the lifetime of a running
// game. The complete name buffer is significant because account checks also
// cover bytes after its first null terminator.
struct FranchiseSession {
    bool completed = false;
    std::array<std::uint8_t, 20> name{};
    PackedAccount packed_account{};
};

} // namespace ghostbusters::game
