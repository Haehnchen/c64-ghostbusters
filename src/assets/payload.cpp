#include "assets/payload.hpp"
#include "assets/embedded.hpp"

#include <stdexcept>
#include <string>

namespace ghostbusters::assets {

Payload Payload::embedded()
{
    return Payload{};
}

std::span<const std::uint8_t> Payload::asset(std::string_view name) const
{
    for (const auto& region : embedded_regions()) {
        if (region.name == name) return region.bytes;
    }
    throw std::out_of_range("Unknown game asset: " + std::string(name));
}

} // namespace ghostbusters::assets
