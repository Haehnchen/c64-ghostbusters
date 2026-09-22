#pragma once

#include "assets/payload.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ghostbusters::assets {

// Native music resources are split by the units consumed by the sequencer.
// Phrase positions below are offsets within the phrase-bank resource, not
// addresses in an original-memory-shaped payload.
class MusicData {
public:
    explicit MusicData(const Payload& payload)
        : frequencies_(payload.asset("audio/music/frequencies")),
          voice_offsets_(payload.asset("audio/music/voice_offsets")),
          sequences_{{payload.asset("audio/music/sequence_0"),
                      payload.asset("audio/music/sequence_1"),
                      payload.asset("audio/music/sequence_2")}},
          phrases_(payload.asset("audio/music/phrases")),
          instruments_(payload.asset("audio/music/instruments"))
    {
        if (frequencies_.size() != 192 || voice_offsets_.size() != 3 ||
            sequences_[0].size() != 110 || sequences_[1].size() != 110 ||
            sequences_[2].size() != 110 || phrases_.size() != 852 ||
            instruments_.size() != 72) {
            throw std::runtime_error("unexpected native music resource size");
        }
    }

    [[nodiscard]] std::uint8_t voice_offset(std::uint8_t channel) const
    {
        if (channel >= voice_offsets_.size()) throw std::out_of_range("music channel");
        return voice_offsets_[channel];
    }

    [[nodiscard]] std::span<const std::uint8_t> sequence(std::uint8_t channel) const
    {
        if (channel >= sequences_.size()) throw std::out_of_range("music channel");
        return sequences_[channel];
    }

    [[nodiscard]] std::uint8_t sequence_byte(std::uint8_t channel,
                                              std::size_t index) const
    {
        const auto bytes = sequence(channel);
        if (index >= bytes.size()) throw std::out_of_range("music sequence");
        return bytes[index];
    }

    [[nodiscard]] std::span<const std::uint8_t> phrase(std::uint8_t number) const
    {
        if (number >= kPhraseOffsets.size() || kPhraseOffsets[number] == kUnusedPhrase)
            throw std::out_of_range("unused or invalid music phrase");
        const auto start = kPhraseOffsets[number];
        for (std::size_t end = start; end < phrases_.size(); ++end) {
            if (phrases_[end] == 0xFFU)
                return phrases_.subspan(start, end - start + 1U);
        }
        throw std::runtime_error("unterminated music phrase");
    }

    [[nodiscard]] std::uint8_t phrase_byte(std::uint8_t number,
                                            std::size_t index) const
    {
        const auto bytes = phrase(number);
        if (index >= bytes.size()) throw std::out_of_range("music phrase");
        return bytes[index];
    }

    [[nodiscard]] std::uint8_t frequency_byte(std::size_t index) const
    {
        if (index >= frequencies_.size()) throw std::out_of_range("music frequency");
        return frequencies_[index];
    }

    [[nodiscard]] std::uint8_t instrument_byte(std::uint8_t instrument,
                                                std::size_t field) const
    {
        const auto index = static_cast<std::size_t>(instrument) * 8U + field;
        if (field >= 8 || index >= instruments_.size())
            throw std::out_of_range("music instrument");
        return instruments_[index];
    }

private:
    static constexpr std::size_t kUnusedPhrase = 852;
    static constexpr std::array<std::size_t, 74> kPhraseOffsets{
        0x000, 0x002, 0x013, 0x020, 0x02D, 0x03D, 0x04C, 0x05E,
        0x06A, 0x078, 0x088, 0x099, 0x0A8, 0x0B9, 0x0C5, 0x0CE,
        0x0D2, 0x0DA, 0x0DE, 0x0E6, 0x0F1, 0x101, 0x105, 0x11A,
        0x127, 0x130, 0x13E, 0x153, 0x165, 0x173, 0x184, 0x196,
        0x1AC, 0x1BD, 0x1D0, 0x1DE, 0x1E9, 0x1F5, kUnusedPhrase, kUnusedPhrase,
        0x203, 0x209, 0x20E, 0x213, 0x216, 0x228, 0x239, 0x242,
        0x24F, 0x258, 0x265, 0x276, 0x27D, 0x282, 0x289, 0x291,
        0x29A, 0x2A1, kUnusedPhrase, kUnusedPhrase, 0x2AA, 0x2AF, 0x2B4, 0x2B9,
        0x2BC, 0x2E1, 0x2F3, 0x315, 0x31F, 0x331, 0x342, kUnusedPhrase,
        kUnusedPhrase, kUnusedPhrase};

    std::span<const std::uint8_t> frequencies_;
    std::span<const std::uint8_t> voice_offsets_;
    std::array<std::span<const std::uint8_t>, 3> sequences_;
    std::span<const std::uint8_t> phrases_;
    std::span<const std::uint8_t> instruments_;
};

} // namespace ghostbusters::assets
