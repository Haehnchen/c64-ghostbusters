#pragma once

#include <cstdint>

namespace ghostbusters::game {

enum class CommonVolume { unchanged, restore_cache, mute };
enum class GameReset { none, full, partial };

// $6FF4-$7005 precedes polling. Activity can restore DEN only on the next
// foreground pass because this samples the previous idle45 value.
[[nodiscard]] constexpr std::uint8_t foreground_display_control(
    std::uint8_t d011, std::uint8_t idle45) noexcept
{
    return static_cast<std::uint8_t>(((d011 | 0x10) & 0x7F) &
                                   ((idle45 & 0x80) != 0 ? 0xEF : 0xFF));
}

// Persistent foreground bytes. Keyboard/joystick scanning precedes this
// module; callers must retain this state across scene changes.
struct InputFrameState {
    std::uint8_t gate02 = 0x80;
    std::uint8_t cycle03 = 0;
    std::uint8_t value04 = 0;
    std::uint8_t value05 = 0;
    std::uint8_t frame09 = 0;
    std::uint8_t delay14 = 0;
    std::uint8_t mode20 = 2;
    std::uint8_t idle45 = 0;
    std::uint8_t idle46 = 0;
    std::uint8_t flags47 = 0;
};

struct InputFrameResult {
    bool continue_frame = false;
    CommonVolume volume = CommonVolume::unchanged;
    GameReset reset = GameReset::none;
};

// $70BA-$70ED: RUN/STOP toggles once per press in States18..41.
[[nodiscard]] CommonVolume update_pause(std::uint8_t& flags47,
    std::uint8_t state3a, std::uint8_t raw_key19);

// $7073-$7105, after $7061-$7072 has supplied raw keyboard position $19.
// $7106 is the continuing boundary; $7103/$8D8D end the foreground frame.
// Reset is reported at $70A4: callers execute $9004's effects separately.
[[nodiscard]] InputFrameResult update_input_frame(InputFrameState& state,
    std::uint8_t counter08, std::uint8_t state3a, std::uint8_t raw_key19);

} // namespace ghostbusters::game
