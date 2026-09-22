#pragma once

#include "assets/payload.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ghostbusters::assets {

// Effect commands retain logical $62..$9D cursors because encoded backward
// branches and saved snapshots use that compact coordinate system. Only the
// effect-owned command bytes and lookup tables are embedded.
class EffectData {
public:
    explicit EffectData(const Payload& payload)
        : delays_(payload.asset("audio/effects/delays")),
          controls_(payload.asset("audio/effects/controls")),
          streams_(payload.asset("audio/effects/streams")),
          pitches_(payload.asset("audio/effects/pitches")),
          frequencies_(payload.asset("audio/effects/frequencies"))
    {
        if (delays_.size() != 24 || controls_.size() != 16 ||
            streams_.size() != kStreamEnd - kStreamBase ||
            pitches_.size() != 96 || frequencies_.size() != 96)
            throw std::runtime_error("unexpected native effect resource size");
    }

    [[nodiscard]] std::size_t stream(std::uint8_t effect) const
    {
        if (effect >= kEffectStreams.size()) throw std::out_of_range("effect stream index");
        return kEffectStreams[effect];
    }

    [[nodiscard]] std::uint8_t stream_byte(std::size_t offset) const
    {
        if (!can_read(offset)) throw std::out_of_range("effect stream access");
        return streams_[offset - kStreamBase];
    }

    [[nodiscard]] std::uint8_t delay(std::uint8_t index) const
    {
        if (index >= delays_.size()) throw std::out_of_range("effect delay");
        return delays_[index];
    }

    [[nodiscard]] std::uint8_t control(std::uint8_t index) const
    {
        if (index >= controls_.size()) throw std::out_of_range("effect control");
        return controls_[index];
    }

    [[nodiscard]] std::uint8_t pitch(std::size_t index) const
    {
        if (index >= pitches_.size()) throw std::out_of_range("effect pitch");
        return pitches_[index];
    }

    [[nodiscard]] std::uint8_t frequency(std::size_t index) const
    {
        if (index >= frequencies_.size()) throw std::out_of_range("effect frequency");
        return frequencies_[index];
    }

    [[nodiscard]] bool contains_cursor(std::size_t offset) const noexcept
    { return offset >= kStreamBase && offset <= kStreamEnd; }

    [[nodiscard]] bool can_read(std::size_t offset) const noexcept
    { return offset >= kStreamBase && offset < kStreamEnd; }

    [[nodiscard]] std::size_t relative_stream(std::size_t offset,
                                              std::uint8_t adjustment) const
    {
        if (!contains_cursor(offset)) throw std::out_of_range("effect stream cursor");
        const auto adjusted = offset + adjustment;
        if (adjusted < 256) throw std::out_of_range("effect relative stream target");
        const auto target = adjusted - 256;
        if (!can_read(target)) throw std::out_of_range("effect relative stream target");
        return target;
    }

private:
    static constexpr std::size_t kStreamBase = 0x62;
    static constexpr std::size_t kStreamEnd = 0x9E;
    static constexpr std::array<std::size_t, 5> kEffectStreams{
        0x62, 0x6C, 0x7A, 0x85, 0x91};

    std::span<const std::uint8_t> delays_;
    std::span<const std::uint8_t> controls_;
    std::span<const std::uint8_t> streams_;
    std::span<const std::uint8_t> pitches_;
    std::span<const std::uint8_t> frequencies_;
};

} // namespace ghostbusters::assets
