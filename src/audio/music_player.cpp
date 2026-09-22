#include "audio/music_player.hpp"

#include <cstddef>

namespace ghostbusters::audio {
namespace {

std::uint8_t decrement(std::uint8_t value) noexcept
{
    return static_cast<std::uint8_t>(value - 1U);
}

} // namespace

MusicPlayer::MusicPlayer(const assets::Payload& payload) : data_(payload)
{
    reset();
}

void MusicPlayer::reset() noexcept
{
    state_.sequence_index.fill(0);
    state_.phrase_index.fill(0);
    state_.duration.fill(0);
}

void MusicPlayer::stop_channels() noexcept
{
    state_.duration.fill(0xFF);
}

bool MusicPlayer::voiceSuppressed(std::uint8_t channel) const noexcept
{
    return voice3_suppressed_ && channel == 2;
}

void MusicPlayer::write(std::vector<SidWrite>& writes, std::uint8_t reg,
                        std::uint8_t value) const
{
    writes.push_back({reg, value});
}

std::vector<SidWrite> MusicPlayer::tick(std::uint8_t mode20,
                                        std::uint8_t counter08,
                                        std::uint8_t disabled47)
{
    std::vector<SidWrite> writes;
    if ((disabled47 & 0x80U) != 0) return writes;

    for (int channel_number = 2; channel_number >= 0; --channel_number) {
        const auto channel = static_cast<std::uint8_t>(channel_number);
        const auto index = static_cast<std::size_t>(channel);
        const auto sid = data_.voice_offset(channel);

        bool load_event = false;
        if ((counter08 & 0x03U) == 0) {
            if ((state_.duration[index] & 0x80U) != 0) continue;
            state_.duration[index] = decrement(state_.duration[index]);
            load_event = (state_.duration[index] & 0x80U) != 0;
        }

        if (load_event) {
            const auto phrase_number =
                data_.sequence_byte(channel, state_.sequence_index[index]);
            if (phrase_number == 0xFFU) {
                state_.duration[index] = 0xFF;
                if (mode20 != 0) reset();
                continue;
            }

            const auto phrase_offset = state_.phrase_index[index];
            const auto command = data_.phrase_byte(phrase_number, phrase_offset);
            state_.command[index] = command;
            state_.duration[index] = static_cast<std::uint8_t>(command & 0x1FU);
            std::uint8_t control_mask = 0xFF;

            if ((command & 0x40U) == 0) {
                state_.phrase_index[index] =
                    static_cast<std::uint8_t>(state_.phrase_index[index] + 1U);
                if ((command & 0x80U) != 0) {
                    const auto metadata =
                        data_.phrase_byte(phrase_number, phrase_offset + 2U);
                    state_.instrument[index] =
                        static_cast<std::uint8_t>(metadata & 0x0FU);
                    auto volume = static_cast<std::uint8_t>(metadata >> 4U);
                    if (volume == 0x0FU) {
                        const auto derived = static_cast<std::uint8_t>(
                            0x6DU - state_.sequence_index[0]);
                        volume = derived < 0x0FU ? derived : 0x0F;
                    }
                    if (mode20 != 0) volume = static_cast<std::uint8_t>(volume >> 1U);
                    state_.volume = volume;
                    write(writes, 0x18, volume);
                    state_.phrase_index[index] =
                        static_cast<std::uint8_t>(state_.phrase_index[index] + 1U);
                }

                const auto note =
                    data_.phrase_byte(phrase_number, phrase_offset + 1U);
                state_.note[index] = note;
                const auto frequency_index = static_cast<std::uint16_t>(note * 2U);
                if (!voiceSuppressed(channel)) {
                    write(writes, static_cast<std::uint8_t>(sid + 1U),
                          data_.frequency_byte(frequency_index + 1U));
                    write(writes, sid,
                          data_.frequency_byte(frequency_index));
                }
            } else {
                control_mask = 0xFE;
            }

            const auto instrument = state_.instrument[index];
            const auto control = data_.instrument_byte(instrument, 2);
            if (!voiceSuppressed(channel)) {
                write(writes, static_cast<std::uint8_t>(sid + 4U),
                      static_cast<std::uint8_t>(control & control_mask));
                write(writes, static_cast<std::uint8_t>(sid + 2U),
                      data_.instrument_byte(instrument, 0));
                write(writes, static_cast<std::uint8_t>(sid + 3U),
                      data_.instrument_byte(instrument, 1));
                write(writes, static_cast<std::uint8_t>(sid + 5U),
                      data_.instrument_byte(instrument, 3));
                write(writes, static_cast<std::uint8_t>(sid + 6U),
                      data_.instrument_byte(instrument, 4));
            }
            state_.control[index] = control;
            state_.phrase_index[index] =
                static_cast<std::uint8_t>(state_.phrase_index[index] + 1U);
            if (data_.phrase_byte(phrase_number, state_.phrase_index[index]) ==
                0xFFU) {
                state_.phrase_index[index] = 0;
                state_.sequence_index[index] =
                    static_cast<std::uint8_t>(state_.sequence_index[index] + 1U);
            }
            // $94FC jumps directly to $958C after loading an event.  The
            // modulation path at $951F-$9589 runs only on non-load ticks.
            continue;
        } else if ((counter08 & 0x03U) == 0) {
            if ((state_.command[index] & 0x20U) == 0 &&
                !voiceSuppressed(channel)) {
                write(writes, static_cast<std::uint8_t>(sid + 4U),
                      static_cast<std::uint8_t>(state_.control[index] & 0xFEU));
                if (state_.duration[index] == 0) {
                    write(writes, static_cast<std::uint8_t>(sid + 5U), 0);
                    write(writes, static_cast<std::uint8_t>(sid + 6U), 0);
                }
            }
        }

        auto shifts = data_.instrument_byte(state_.instrument[index], 5);
        if (shifts == 0) continue;

        auto triangle = static_cast<std::uint8_t>(counter08 & 0x07U);
        if (triangle >= 4) triangle = static_cast<std::uint8_t>(triangle ^ 0x07U);
        const auto note_index = static_cast<std::uint16_t>(state_.note[index] * 2U);
        const auto low = data_.frequency_byte(note_index);
        const auto high = data_.frequency_byte(note_index + 1U);
        const auto next_low = data_.frequency_byte(note_index + 2U);
        const auto next_high = data_.frequency_byte(note_index + 3U);
        auto delta = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(next_high) << 8U | next_low) -
            (static_cast<std::uint16_t>(high) << 8U | low));
        do {
            delta = static_cast<std::uint16_t>(delta >> 1U);
            shifts = decrement(shifts);
        } while ((shifts & 0x80U) == 0);

        auto frequency = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(high) << 8U | low);
        if ((state_.command[index] & 0x1FU) >= 5U) {
            for (std::uint8_t step = 0; step < triangle; ++step) {
                frequency = static_cast<std::uint16_t>(frequency + delta);
            }
        }
        if (!voiceSuppressed(channel)) {
            write(writes, sid, static_cast<std::uint8_t>(frequency));
            write(writes, static_cast<std::uint8_t>(sid + 1U),
                  static_cast<std::uint8_t>(frequency >> 8U));
        }
    }
    return writes;
}

MusicPlayerSnapshot MusicPlayer::stateSnapshot() const noexcept
{
    return state_;
}

void MusicPlayer::restoreState(const MusicPlayerSnapshot& snapshot) noexcept
{
    state_ = snapshot;
}

void MusicPlayer::setVoice3Suppressed(bool suppressed) noexcept
{
    voice3_suppressed_ = suppressed;
}

} // namespace ghostbusters::audio
