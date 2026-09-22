#pragma once

#include "audio/music_player.hpp"

#include <cstdint>
#include <vector>

namespace ghostbusters::audio {

// The short SID register sequences used by the text/input routines. The
// volume cache mirrors $EA82, which is updated only when text_tone() has to
// restore volume 15 to $D418.
class TextSound {
public:
    explicit TextSound(std::uint8_t volume = 0) noexcept : volume_(volume) {}

    // Mirrors the text tone at $7306-$733A. The returned writes are in the
    // original order and the final voice-3 gate write is always included.
    [[nodiscard]] std::vector<SidWrite> text_tone();

    // Mirrors the two voice-3 gate writes used for keyboard input at $734F.
    // Frequency, pulse width and envelope deliberately remain whatever the
    // text or music path most recently programmed for voice 3.
    [[nodiscard]] std::vector<SidWrite> key_click();

    [[nodiscard]] std::uint8_t volume() const noexcept { return volume_; }

private:
    std::uint8_t volume_;
};

} // namespace ghostbusters::audio
