#include "game/account_number.hpp"

#include <cstddef>
#include <cstdint>

namespace ghostbusters::game {

std::array<std::uint8_t, 4>
pack_account_number(std::span<const std::uint8_t> input)
{
    std::uint32_t packed = 0;
    const auto count = input.size() < 8 ? input.size() : 8;
    // Input is nibble-packed, not parsed as decimal; preserve non-digit aliases.
    for (std::size_t index = 0; index < count; ++index) {
        const auto value = input[index];
        if (value == 0) break;
        packed = (packed << 4) | static_cast<std::uint32_t>(value & 0x0F);
    }

    return {
        static_cast<std::uint8_t>((packed >> 24) & 0xFF),
        static_cast<std::uint8_t>((packed >> 16) & 0xFF),
        static_cast<std::uint8_t>((packed >> 8) & 0xFF),
        static_cast<std::uint8_t>(packed & 0xFF),
    };
}

} // namespace ghostbusters::game
