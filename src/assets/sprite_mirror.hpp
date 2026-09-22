#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ghostbusters::assets {

// Build the two horizontally mirrored four-sprite sheets used by the capture
// and rooftop scenes. The source and destination ranges share one scene buffer.
inline void mirror_sprite_sheets(std::span<std::uint8_t> scene)
{
    constexpr std::size_t kSceneSize = 0x1100;
    if (scene.size() < kSceneSize) {
        throw std::out_of_range("mirrored sprite scene is too short");
    }

    const auto reverse_pairs = [](std::uint8_t value) {
        return static_cast<std::uint8_t>(((value & 0x03U) << 6U) |
                                         ((value & 0x0CU) << 2U) |
                                         ((value & 0x30U) >> 2U) |
                                         ((value & 0xC0U) >> 6U));
    };

    std::uint8_t x = 0xFC;
    for (;;) {
        const auto offset = static_cast<std::size_t>(x);
        scene[0x0F02 + offset] = reverse_pairs(scene[0x0640 + offset]);
        scene[0x0F01 + offset] = reverse_pairs(scene[0x0641 + offset]);
        scene[0x0F00 + offset] = reverse_pairs(scene[0x0642 + offset]);
        scene[0x1002 + offset] = reverse_pairs(scene[0x0740 + offset]);
        scene[0x1001 + offset] = reverse_pairs(scene[0x0741 + offset]);
        scene[0x1000 + offset] = reverse_pairs(scene[0x0742 + offset]);
        x = static_cast<std::uint8_t>(x - 3U);
        if (x == 0xFDU) break;
        if ((x & 0x3FU) == 0x3DU) {
            --x;
            if (x == 0) break;
        }
    }
}

} // namespace ghostbusters::assets
