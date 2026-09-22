#pragma once

#include "assets/payload.hpp"
#include "video/sprite_layer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ghostbusters::game {

// Functional native counterpart of the title's sprite-0 movement.  One tick
// is one foreground title update; raster timing remains outside this class.
class TitleBall {
public:
    TitleBall(const assets::Payload& payload,
              std::span<const std::uint8_t> decoded_scene);

    void tick();

    [[nodiscard]] video::SpriteLayer sprite_layer() const;

    // Logical state is exposed for focused parity tests and diagnostics.
    [[nodiscard]] std::uint16_t x_register() const noexcept;
    [[nodiscard]] std::uint8_t y_register() const noexcept { return y_; }
    [[nodiscard]] std::uint8_t phase() const noexcept { return phase_; }
    [[nodiscard]] std::size_t command_index() const noexcept { return command_index_; }

private:
    std::span<const std::uint8_t> y_deltas_;
    std::span<const std::uint8_t> commands_;
    video::Sprite sprite_;
    std::uint8_t x_low_ = 0;
    std::uint8_t x_high_ = 0;
    std::uint8_t y_ = 0xBA;
    std::uint8_t phase_ = 0;
    std::uint8_t countdown_ = 0;
    std::uint8_t velocity_ = 0;
    std::size_t command_index_ = 0;
};

} // namespace ghostbusters::game
