#pragma once

#include "assets/effect_data.hpp"
#include "audio/music_player.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ghostbusters::audio {

struct EffectPlayerSnapshot {
    std::uint16_t stream_cursor = 0;
    bool stream_cursor_valid = false;
    std::uint8_t timer = 0;
    std::uint8_t timer_match = 0;
    std::uint8_t pitch = 0;
    std::uint8_t control_cache = 0;
    std::uint8_t modulation = 0;
    std::uint8_t modulation_period = 0;
    std::uint8_t sweep_period = 0;
    bool active = false;
    bool voice3_suppressed = false;

    [[nodiscard]] bool operator==(const EffectPlayerSnapshot&) const = default;
};

// Native port of the small voice-3 effect engine.  One tick corresponds to
// one invocation from the normal audio scheduler; returned writes contain
// only SID bus stores while decoder/cache updates stay internal.
class EffectPlayer {
public:
    explicit EffectPlayer(const assets::Payload& payload);

    // Start one of the five exported effect streams.  Starting an already
    // active effect is a no-op, matching the busy-start guard.
    [[nodiscard]] std::vector<SidWrite> start(std::uint8_t index);

    // Always writes voice-3 control 0, including when already inactive.
    [[nodiscard]] std::vector<SidWrite> stop();

    // Advance one scheduled effect tick.
    [[nodiscard]] std::vector<SidWrite> tick();

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool voice3_suppressed() const noexcept { return voice3_suppressed_; }
    [[nodiscard]] EffectPlayerSnapshot stateSnapshot() const noexcept;
    // Active snapshots require a readable native stream cursor.
    void restoreState(const EffectPlayerSnapshot& snapshot);

private:
    [[nodiscard]] std::uint8_t readStreamByte();
    void processStream(std::vector<SidWrite>& writes);
    void write(std::vector<SidWrite>& writes, std::uint8_t reg,
               std::uint8_t value) const;

    assets::EffectData data_;
    std::size_t stream_offset_ = 0;
    bool stream_cursor_valid_ = false;
    std::uint8_t timer_ = 0;
    std::uint8_t timer_match_ = 0;
    std::uint8_t pitch_ = 0;
    std::uint8_t control_cache_ = 0;
    std::uint8_t modulation_ = 0;
    std::uint8_t modulation_period_ = 0;
    std::uint8_t sweep_period_ = 0;
    bool active_ = false;
    bool voice3_suppressed_ = false;
};

} // namespace ghostbusters::audio
