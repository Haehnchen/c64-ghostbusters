#pragma once

#include "assets/payload.hpp"
#include "video/character_frame.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ghostbusters::game {

// Prepared shared sprite bitmaps, one 64-byte VIC sprite per pointer.
[[nodiscard]] std::vector<std::uint8_t> shared_sprite_data(
    const assets::Payload& payload);

struct TitleScreen {
    video::CharacterFrame characters;
    std::vector<std::uint8_t> scene_data;
    std::array<std::uint8_t, 24> rainbow{};
    [[nodiscard]] auto footer_rainbow() const
    {
        return std::span<const std::uint8_t>(rainbow);
    }
};

// Prepare the title character layer and shared sprite graphics before animation.
[[nodiscard]] TitleScreen prepare_title_screen(const assets::Payload& payload,
                                              std::span<const std::uint8_t> font);

} // namespace ghostbusters::game
