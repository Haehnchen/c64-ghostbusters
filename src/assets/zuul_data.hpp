#pragma once

#include "assets/capture_data.hpp"
#include "assets/payload.hpp"
#include "assets/sprite_mirror.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ghostbusters::assets {

// Named, bounded view over the graphics and lookup tables used by Zuul
// States $28-$2C. The state machine consumes gate frames, rooftop pointers
// and complete climb rows.
class ZuulData {
public:
    struct GateFrame {
        std::uint8_t y;
        std::uint8_t x;
        std::uint8_t packed_pointers;
    };

    struct ClimbRow {
        std::array<std::uint8_t, 40> screen{};
        std::array<std::uint8_t, 40> colors{};
    };

    explicit ZuulData(const Payload& payload)
        : capture_(payload), gate_(payload.asset("zuul/gate_frames")),
          generated_screen_(payload.asset("zuul/generated_screen")),
          generated_colors_(payload.asset("zuul/generated_colors")),
          climb_screen_(payload.asset("zuul/climb_screen")),
          climb_colors_(payload.asset("zuul/climb_colors"))
    {
        if (gate_.size() != 32 * 3 || generated_screen_.size() != 3 * 40 ||
            generated_colors_.size() != 3 * 40 || climb_screen_.size() != 21 * 40 ||
            climb_colors_.size() != 21 * 40)
            throw std::runtime_error("Unexpected Zuul graphics dimensions");
    }

    [[nodiscard]] std::span<const std::uint8_t> scene_graphics() const noexcept
    {
        return capture_.scene_graphics();
    }

    [[nodiscard]] std::uint8_t sprite_color(std::uint8_t pointer) const
    {
        return capture_.sprite_color(pointer);
    }

    // Reproduce the two horizontally mirrored four-sprite sheets. Accepting
    // only the exact native destination size prevents partial output.
    void mirror_sprite_sheets(std::span<std::uint8_t> scene) const
    {
        if (scene.size() != kMirroredSceneSize) {
            throw std::out_of_range("Zuul mirrored sprite scene size");
        }
        ghostbusters::assets::mirror_sprite_sheets(scene);
    }

    [[nodiscard]] GateFrame gate_frame(std::uint8_t phase) const
    {
        if (phase >= kGateFrameCount) {
            throw std::out_of_range("Zuul gate phase");
        }
        return {gate_[phase * 3], gate_[phase * 3 + 1], gate_[phase * 3 + 2]};
    }

    [[nodiscard]] std::uint8_t rooftop_pointer(std::size_t sprite) const
    {
        if (sprite >= kRooftopPointerCount) {
            throw std::out_of_range("Zuul rooftop sprite");
        }
        return static_cast<std::uint8_t>(0x2B + sprite);
    }

    [[nodiscard]] ClimbRow generated_climb_row(std::uint8_t phase) const
    {
        if (phase >= kGeneratedPhaseCount) {
            throw std::out_of_range("Zuul generated climb phase");
        }
        return row_at(generated_screen_, generated_colors_, phase);
    }

    [[nodiscard]] ClimbRow static_climb_row(std::uint8_t row_index) const
    {
        if (row_index >= kStaticRowCount) {
            throw std::out_of_range("Zuul static climb row");
        }
        return row_at(climb_screen_, climb_colors_, row_index);
    }

private:
    static constexpr std::size_t kMirroredSceneSize = 0x1100;
    static constexpr std::size_t kGateFrameCount = 32;
    static constexpr std::size_t kRooftopPointerCount = 4;
    static constexpr std::size_t kGeneratedPhaseCount = 3;
    static constexpr std::size_t kStaticRowCount = 21;

    static ClimbRow row_at(std::span<const std::uint8_t> screen,
                           std::span<const std::uint8_t> colors, std::size_t row_index)
    {
        ClimbRow row;
        // Screen codes and palette indices are prepared in display order.
        std::copy_n(screen.begin() + row_index * 40, 40, row.screen.begin());
        std::copy_n(colors.begin() + row_index * 40, 40, row.colors.begin());
        return row;
    }

    CaptureData capture_;
    std::span<const std::uint8_t> gate_, generated_screen_, generated_colors_;
    std::span<const std::uint8_t> climb_screen_, climb_colors_;
};

} // namespace ghostbusters::assets
