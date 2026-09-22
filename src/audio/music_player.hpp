#pragma once

#include "assets/music_data.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::audio {

struct SidWrite {
    std::uint8_t reg = 0;
    std::uint8_t value = 0;

    [[nodiscard]] bool operator==(const SidWrite&) const = default;
};

struct MusicPlayerSnapshot {
    std::array<std::uint8_t, 3> sequence_index{};
    std::array<std::uint8_t, 3> phrase_index{};
    std::array<std::uint8_t, 3> duration{};
    std::array<std::uint8_t, 3> note{};
    std::array<std::uint8_t, 3> command{};
    std::array<std::uint8_t, 3> control{};
    std::array<std::uint8_t, 3> instrument{};
    std::uint8_t volume = 0;

    [[nodiscard]] bool operator==(const MusicPlayerSnapshot&) const = default;
};

// Direct port of $93E2/$93F0-$9592. One tick is one original routine call;
// the caller supplies $20, $08 and $47 and owns all frame scheduling.
class MusicPlayer {
public:
    explicit MusicPlayer(const assets::Payload& payload);

    // Mirrors $93E2: only sequence, phrase and duration indices are cleared.
    void reset() noexcept;

    // Mirrors the stores at $8DA9-$8DAF after $93E2: stop all three channels
    // while retaining the note, command, control, instrument and volume
    // caches used by the following IRQ path.
    void stop_channels() noexcept;

    [[nodiscard]] std::vector<SidWrite> tick(std::uint8_t mode20,
                                             std::uint8_t counter08,
                                             std::uint8_t disabled47);

    [[nodiscard]] MusicPlayerSnapshot stateSnapshot() const noexcept;
    void restoreState(const MusicPlayerSnapshot& snapshot) noexcept;

    // Mirrors $98. A nonzero value suppresses SID voice-3 writes, except D418.
    void setVoice3Suppressed(bool suppressed) noexcept;

private:
    [[nodiscard]] bool voiceSuppressed(std::uint8_t channel) const noexcept;
    void write(std::vector<SidWrite>& writes, std::uint8_t reg,
               std::uint8_t value) const;

    assets::MusicData data_;
    MusicPlayerSnapshot state_{};
    bool voice3_suppressed_ = false;
};

} // namespace ghostbusters::audio
