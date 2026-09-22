#pragma once

#include <cstdint>
#include <vector>

namespace ghostbusters::audio {

struct SpeechEvent {
    // Logical exported timer index; gaps represent intervals with no write.
    std::uint32_t timer_tick = 0;
    std::uint8_t value = 0;
    [[nodiscard]] bool operator==(const SpeechEvent&) const = default;
};

struct DecodedSpeech {
    std::uint16_t timer_latch = 0;
    std::uint32_t total_timer_ticks = 0;
    std::vector<SpeechEvent> events;
};

} // namespace ghostbusters::audio
