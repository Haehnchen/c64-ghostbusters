#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace ghostbusters::assets {

struct EmbeddedRegion {
    std::string_view name;
    std::span<const std::uint8_t> bytes;
};

// Product assets have names and bounded byte spans, not source addresses.
[[nodiscard]] std::span<const EmbeddedRegion> embedded_regions();
[[nodiscard]] std::span<const std::uint8_t> embedded_font();

} // namespace ghostbusters::assets
