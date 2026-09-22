#include "audio/effect_player.hpp"

#include <stdexcept>

namespace ghostbusters::audio {
namespace {

constexpr std::uint8_t kEffectCount = 5;
constexpr std::uint16_t kMaxCommandsPerTick = 256;

} // namespace

EffectPlayer::EffectPlayer(const assets::Payload& payload) : data_(payload) {}

std::vector<SidWrite> EffectPlayer::start(const std::uint8_t index)
{
    if (index >= kEffectCount) {
        throw std::out_of_range("Effect index is outside the stream list");
    }
    if (active_) return {};

    // Reset only the fields written by the source start routine. Pitch and
    // sweep-period caches deliberately carry over from the preceding effect.
    // Voice 3 is cleared by the ordered writes returned below.
    timer_ = 1;
    timer_match_ = 0xFF;
    control_cache_ = 0;
    modulation_ = 0;
    modulation_period_ = 0;
    stream_offset_ = data_.stream(index);
    stream_cursor_valid_ = true;
    active_ = true;
    voice3_suppressed_ = true;

    return {{0x13, 0x00}, {0x14, 0x00}, {0x11, 0x00}, {0x10, 0x00},
            {0x12, 0x00}};
}

std::vector<SidWrite> EffectPlayer::stop()
{
    // Keep the stream cursor and other effect caches untouched.
    active_ = false;
    timer_ = 0;
    voice3_suppressed_ = false;
    return {{0x12, 0x00}};
}

std::vector<SidWrite> EffectPlayer::tick()
{
    std::vector<SidWrite> writes;
    if (!active_) return writes;

    // Process a command only on the zero transition. A zero timer wraps as
    // an 8-bit decrement, just like the source decoder.
    timer_ = static_cast<std::uint8_t>(timer_ - 1U);
    if (timer_ == 0) {
        processStream(writes);
    } else if (timer_ == timer_match_) {
        // Refresh control before the post-command modulation block.
        write(writes, 0x12, control_cache_);
    }

    // Apply post-command modulation on every active tick, including the
    // terminating tick.
    if (modulation_ != 0) {
        // The EOR result is written to SID while the incremented value is kept
        // as the next modulation state.
        const auto inverted = static_cast<std::uint8_t>(modulation_ ^ 0xFFU);
        modulation_ = static_cast<std::uint8_t>(inverted + 1U);
        write(writes, 0x0F,
              static_cast<std::uint8_t>(inverted + pitch_ + 1U));
    }
    if (modulation_period_ != 0) {
        if ((modulation_period_ & 0x80U) != 0) {
            pitch_ = static_cast<std::uint8_t>(pitch_ - 1U);
        } else {
            pitch_ = static_cast<std::uint8_t>(pitch_ + 1U);
        }
        write(writes, 0x0F, pitch_);
    }
    return writes;
}

EffectPlayerSnapshot EffectPlayer::stateSnapshot() const noexcept
{
    return {static_cast<std::uint16_t>(stream_offset_), stream_cursor_valid_,
            timer_, timer_match_, pitch_, control_cache_,
            modulation_, modulation_period_, sweep_period_, active_,
            voice3_suppressed_};
}

void EffectPlayer::restoreState(const EffectPlayerSnapshot& snapshot)
{
    if (snapshot.active &&
        (!snapshot.stream_cursor_valid ||
         !data_.can_read(snapshot.stream_cursor))) {
        throw std::out_of_range(
            "Active effect snapshot has no readable stream cursor");
    }
    if (snapshot.stream_cursor_valid &&
        !data_.contains_cursor(snapshot.stream_cursor)) {
        throw std::out_of_range("Effect snapshot stream cursor is outside the asset");
    }
    stream_offset_ = snapshot.stream_cursor_valid ? snapshot.stream_cursor : 0;
    stream_cursor_valid_ = snapshot.stream_cursor_valid;
    timer_ = snapshot.timer;
    timer_match_ = snapshot.timer_match;
    pitch_ = snapshot.pitch;
    control_cache_ = snapshot.control_cache;
    modulation_ = snapshot.modulation;
    modulation_period_ = snapshot.modulation_period;
    sweep_period_ = snapshot.sweep_period;
    active_ = snapshot.active;
    voice3_suppressed_ = snapshot.voice3_suppressed;
}

std::uint8_t EffectPlayer::readStreamByte()
{
    const auto value = data_.stream_byte(stream_offset_);
    stream_offset_ += 1U;
    return value;
}

void EffectPlayer::processStream(std::vector<SidWrite>& writes)
{
    for (std::uint16_t command_count = 0; command_count < kMaxCommandsPerTick;
         ++command_count) {
        const auto command = readStreamByte();
        const auto argument = static_cast<std::uint8_t>(command >> 4U);
        const auto opcode = static_cast<std::uint8_t>(command & 0x0FU);

        // A zero opcode terminates the effect.
        if (opcode == 0) {
            timer_ = 0;
            active_ = false;
            voice3_suppressed_ = false;
            return;
        }

        switch (opcode) {
        case 1: // delay only
            timer_ = data_.delay(argument);
            break;
        case 2: // delay/gate, then load a note
            timer_ = data_.delay(argument);
            write(writes, 0x12, static_cast<std::uint8_t>(control_cache_ | 0x01U));
            [[fallthrough]];
        case 6: { // load frequency from the next stream byte
            const auto note = readStreamByte();
            write(writes, 0x0E, data_.frequency(note));
            pitch_ = data_.pitch(note);
            write(writes, 0x0F, pitch_);
            break;
        }
        case 5: { // secondary dispatch indexed by command high nibble
            switch (static_cast<std::uint8_t>(argument & 0x07U)) {
            case 0: // load sustain/release and pulse width
                write(writes, 0x13, readStreamByte());
                write(writes, 0x14, readStreamByte());
                break;
            case 1: // set signed pitch-sweep direction
                modulation_period_ = command;
                break;
            case 2: { // conditional relative stream-pointer jump
                const auto adjustment = readStreamByte();
                sweep_period_ = static_cast<std::uint8_t>(sweep_period_ - 1U);
                if (sweep_period_ != 0) {
                    stream_offset_ = data_.relative_stream(stream_offset_, adjustment);
                }
                break;
            }
            default: // one-byte return for secondary indices 3..7
                break;
            }
            break;
        }
        case 7: // modulation direction/period
            modulation_ = argument;
            break;
        case 0x0A: // sweep period
            sweep_period_ = argument;
            break;
        case 0x0B: // high then low frequency byte
            write(writes, 0x11, argument);
            write(writes, 0x10, readStreamByte());
            break;
        case 0x0C: { // control lookup and cached gate bit
            const auto control = data_.control(argument);
            write(writes, 0x12, control);
            control_cache_ = static_cast<std::uint8_t>(control & 0xFEU);
            break;
        }
        case 0x0D: { // signed high-nibble pitch delta
            auto delta = argument;
            if ((command & 0x80U) != 0) delta = static_cast<std::uint8_t>(delta | 0xF0U);
            pitch_ = static_cast<std::uint8_t>(pitch_ + delta);
            write(writes, 0x0F, pitch_);
            break;
        }
        // Opcodes 3, 4, 8, 9, E and F are one-byte returns.
        case 3:
        case 4:
        case 8:
        case 9:
        case 0x0E:
        case 0x0F:
            break;
        default:
            // Keep malformed streams from inventing a SID operation or
            // spinning forever in the frame scheduler.
            return;
        }

        // Nibbles 1 and 2 return after arming the timer; all other implemented
        // commands continue consuming same-tick command bytes.
        if (opcode < 4) return;
    }

    // Valid streams reach a terminator or timed opcode before this bound.
    timer_ = 0;
    active_ = false;
    voice3_suppressed_ = false;
}

void EffectPlayer::write(std::vector<SidWrite>& writes, const std::uint8_t reg,
                         const std::uint8_t value) const
{
    writes.push_back({reg, value});
}

} // namespace ghostbusters::audio
