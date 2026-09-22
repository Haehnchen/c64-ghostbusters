#include "audio/speech_assets.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

// This file is deliberately the only product-side reader for the generated
// speech asset.  The include contains constexpr bytes, so no working-directory
// or external asset lookup is involved at runtime.
#include "speech_assets.inc"

namespace ghostbusters::audio {
namespace {

constexpr std::size_t kHeaderBytes = 8;
// GBSA header: magic[4], version, command count, two reserved bytes.
// Each little-endian index entry is u16 timer latch, u16 reserved, then
// u32 total ticks, event count, byte offset and encoded length. Events pair
// an unsigned base-128 tick delta with one volume byte; offsets address the
// blob. Deltas start from logical tick zero, so only the first event may have
// a zero delta; later events are strictly increasing.
constexpr std::size_t kEntryBytes = 20;
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kCommandCount = 5;

[[nodiscard]] std::uint16_t read16(std::size_t offset)
{
    return static_cast<std::uint16_t>(detail::kSpeechAssetBlob[offset] |
                                      (detail::kSpeechAssetBlob[offset + 1] << 8U));
}

[[nodiscard]] std::uint32_t read32(std::size_t offset)
{
    return static_cast<std::uint32_t>(detail::kSpeechAssetBlob[offset]) |
           (static_cast<std::uint32_t>(detail::kSpeechAssetBlob[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(detail::kSpeechAssetBlob[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(detail::kSpeechAssetBlob[offset + 3]) << 24U);
}

void validate_blob()
{
    if (detail::kSpeechAssetBlob.size() < kHeaderBytes ||
        detail::kSpeechAssetBlob[0] != 'G' || detail::kSpeechAssetBlob[1] != 'B' ||
        detail::kSpeechAssetBlob[2] != 'S' || detail::kSpeechAssetBlob[3] != 'A' ||
        detail::kSpeechAssetBlob[4] != kVersion ||
        detail::kSpeechAssetBlob[5] != kCommandCount) {
        throw std::runtime_error("embedded speech asset has an invalid header");
    }
    const auto table_end = kHeaderBytes + kCommandCount * kEntryBytes;
    if (detail::kSpeechAssetBlob.size() < table_end) {
        throw std::runtime_error("embedded speech asset has a truncated index");
    }
    for (std::size_t index = 0; index < kCommandCount; ++index) {
        const auto entry = kHeaderBytes + index * kEntryBytes;
        const auto offset = read32(entry + 12);
        const auto size = read32(entry + 16);
        if (offset < table_end || offset > detail::kSpeechAssetBlob.size() ||
            size > detail::kSpeechAssetBlob.size() - offset) {
            throw std::runtime_error("embedded speech asset index points outside its blob");
        }
    }
}

[[nodiscard]] std::size_t checked_entry(std::uint8_t command)
{
    if (command >= kCommandCount) {
        throw std::invalid_argument("speech asset command must be in the range 0..4");
    }
    validate_blob();
    return kHeaderBytes + static_cast<std::size_t>(command) * kEntryBytes;
}

[[nodiscard]] std::uint32_t read_varuint(std::size_t& cursor, std::size_t end)
{
    std::uint32_t value = 0;
    unsigned shift = 0;
    while (cursor < end && shift <= 28) {
        const auto byte = detail::kSpeechAssetBlob[cursor++];
        const auto part = static_cast<std::uint32_t>(byte & 0x7FU);
        if (part > (std::numeric_limits<std::uint32_t>::max() >> shift)) {
            throw std::runtime_error("embedded speech event tick overflows uint32_t");
        }
        value |= part << shift;
        if ((byte & 0x80U) == 0) return value;
        shift += 7;
    }
    throw std::runtime_error("embedded speech event has an invalid varint");
}

} // namespace

SpeechAssetInfo speech_asset_info(std::uint8_t command)
{
    const auto entry = checked_entry(command);
    return {command, read16(entry), read32(entry + 4), read32(entry + 8),
            read32(entry + 16)};
}

DecodedSpeech load_speech_asset(std::uint8_t command)
{
    const auto entry = checked_entry(command);
    const SpeechAssetInfo info{command, read16(entry), read32(entry + 4),
                               read32(entry + 8), read32(entry + 16)};
    auto cursor = static_cast<std::size_t>(read32(entry + 12));
    const auto end = cursor + info.encoded_bytes;
    DecodedSpeech result;
    result.timer_latch = info.timer_latch;
    result.total_timer_ticks = info.total_timer_ticks;
    result.events.reserve(info.event_count);

    std::uint32_t previous_tick = 0;
    for (std::uint32_t index = 0; index < info.event_count; ++index) {
        const auto delta = read_varuint(cursor, end);
        if (delta > std::numeric_limits<std::uint32_t>::max() - previous_tick) {
            throw std::runtime_error("embedded speech event tick overflows uint32_t");
        }
        const auto tick = previous_tick + delta;
        if (tick >= info.total_timer_ticks || cursor >= end) {
            throw std::runtime_error("embedded speech event tick is outside its stream");
        }
        const auto value = detail::kSpeechAssetBlob[cursor++];
        result.events.push_back({tick, value});
        previous_tick = tick;
    }
    if (cursor != end) {
        throw std::runtime_error("embedded speech asset contains trailing event bytes");
    }
    return result;
}

} // namespace ghostbusters::audio
