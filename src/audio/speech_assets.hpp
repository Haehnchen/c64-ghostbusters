#pragma once

#include "audio/speech_types.hpp"

#include <cstdint>

namespace ghostbusters::audio {

// Metadata for one exported logical D418 stream.  The event payload is
// embedded in the program; this view is intentionally independent of the
// reference payload and of the 6502 decoder.
struct SpeechAssetInfo {
    std::uint8_t command = 0;
    std::uint16_t timer_latch = 0;
    std::uint32_t total_timer_ticks = 0;
    std::uint32_t event_count = 0;
    std::uint32_t encoded_bytes = 0;

    [[nodiscard]] bool operator==(const SpeechAssetInfo&) const = default;
};

// Return exported metadata for one of the five canonical speech commands.
// Throws std::invalid_argument when command is outside 0..4.
[[nodiscard]] SpeechAssetInfo speech_asset_info(std::uint8_t command);

// Decode the compact, embedded event stream into the same logical form used
// by SceneAudio.  This is a data unpacker, not a CPU/6502 implementation and
// does not access a Payload or any file at runtime.
[[nodiscard]] DecodedSpeech load_speech_asset(std::uint8_t command);

} // namespace ghostbusters::audio
