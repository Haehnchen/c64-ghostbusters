#include "game/title_ball.hpp"

#include <algorithm>
#include <stdexcept>

namespace ghostbusters::game {
namespace {

constexpr std::size_t kDeltaCount = 16;
constexpr std::size_t kCommandCount = 0x125;
constexpr std::size_t kBallBitmapOffset = 0x340;
constexpr std::size_t kSpriteBytes = 63;
constexpr int kVicVisibleX = 24;
constexpr int kVicVisibleY = 50;

} // namespace

TitleBall::TitleBall(const assets::Payload& payload,
                     const std::span<const std::uint8_t> decoded_scene)
    : y_deltas_(payload.asset("title/ball_motion")),
      commands_(payload.asset("title/ball_path"))
{
    if (y_deltas_.size() != kDeltaCount)
        throw std::runtime_error("Title-ball Y delta table has an unexpected size");
    if (commands_.size() != kCommandCount)
        throw std::runtime_error("Title-ball command stream has an unexpected size");
    if (decoded_scene.size() < kBallBitmapOffset + kSpriteBytes)
        throw std::runtime_error("Decoded title scene does not contain the ball sprite");

    std::copy_n(decoded_scene.begin() + kBallBitmapOffset, kSpriteBytes,
                sprite_.bitmap.begin());
    sprite_.color = 1;
    sprite_.enabled = true;
}

void TitleBall::tick()
{
    const auto old_phase = phase_;
    if (old_phase == 0) {
        --countdown_;
        if ((countdown_ & 0x80U) != 0) {
            countdown_ = 0;
            const auto command = commands_[command_index_];
            if (command_index_ + 1 < commands_.size()) ++command_index_;

            if ((command & 0x80U) != 0) {
                // BIT/BVC/AND #$BF normalizes bit 6 before the saved raw N
                // flag selects the delay-command path.
                auto delay = static_cast<std::uint8_t>(command & 0xBFU);
                if (delay == 0xBF) {
                    x_low_ = 0;
                    x_high_ = 0;
                    // The terminator's LDA #0 precedes AND #$7F, so it is
                    // reread at the next phase-zero tick rather than waiting.
                    delay = 0;
                }
                countdown_ = static_cast<std::uint8_t>(delay & 0x7FU);
                velocity_ = 0;
            } else {
                velocity_ = static_cast<std::uint8_t>(command & 0xBFU);
            }
        }
    }

    y_ = static_cast<std::uint8_t>(y_ + y_deltas_[old_phase]);
    phase_ = static_cast<std::uint8_t>((old_phase + 1U) & 0x0FU);

    const auto x = static_cast<std::uint16_t>(x_register() + velocity_);
    x_low_ = static_cast<std::uint8_t>(x);
    x_high_ = static_cast<std::uint8_t>(x >> 8U);
    // The original accepts positions through $0157 and wraps at $0158.
    if (x_high_ != 0 && x_low_ >= 0x58) {
        x_low_ = 0;
        x_high_ = 0;
    }
}

std::uint16_t TitleBall::x_register() const noexcept
{
    return static_cast<std::uint16_t>(x_low_) |
           (static_cast<std::uint16_t>(x_high_) << 8U);
}

video::SpriteLayer TitleBall::sprite_layer() const
{
    video::SpriteLayer layer;
    layer.sprites[0] = sprite_;
    layer.sprites[0].x = static_cast<std::int16_t>(x_register()) - kVicVisibleX;
    // The visible character viewport starts one raster below its nominal
    // origin; use the same VIC-to-image convention as the gameplay renderer.
    layer.sprites[0].y = static_cast<std::int16_t>(y_) + 1 - kVicVisibleY;
    return layer;
}

} // namespace ghostbusters::game
