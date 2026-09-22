#pragma once

#include "assets/embedded.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace ghostbusters::assets {

class Payload {
public:
    static Payload embedded();
    [[nodiscard]] std::span<const std::uint8_t> asset(std::string_view name) const;

private:
    Payload() = default;

};

} // namespace ghostbusters::assets
