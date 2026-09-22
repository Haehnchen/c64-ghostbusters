#include "audio/scene_audio.hpp"
#include "audio/text_sound.hpp"
#include "game/pal_counter.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ghostbusters::audio {

SceneAudio::SceneAudio(const assets::Payload& payload, StartPoint start) : music_(payload), effects_(payload)
{
    volume_ = 0xFF; // Startup cache; normal scene events subsequently set the volume.
    if (start == StartPoint::startup) {
        // Startup queues commands 1 and 3 as one blocking sequence. Normal
        // music/effect ticks resume only after both embedded streams finish.
        speech_[0] = load_speech_asset(1);
        speech_[1] = load_speech_asset(3);
        speech_index_ = 0;
        write_sid(0x18, 7); // Startup audio wrapper's initial volume.
    }
}

void SceneAudio::write_sid(std::uint8_t reg, std::uint8_t value)
{
    if (trace_enabled_) sid_trace_.push_back({reg, value});
    sid_.write(reg, value);
}

std::vector<SidWrite> SceneAudio::take_sid_writes()
{
    return std::exchange(sid_trace_, {});
}

void SceneAudio::leave_title()
{
    // $63AF clears registers, preserving the chip's oscillator/envelope state.
    for (int reg = 31; reg >= 0; --reg) write_sid(static_cast<std::uint8_t>(reg), 0);
    title_ = false;
    // $EA82 is not cleared by this routine: preserve the volume cache.
}

void SceneAudio::synchronize_title_counter(std::uint8_t counter)
{
    if (!title_) throw std::logic_error("Title counter used outside title scene");
    counter_ = counter;
}

void SceneAudio::reset_foreground()
{
    MusicPlayerSnapshot cleared;
    cleared.duration.fill(0xFF);
    cleared.volume = volume_; // EA82 lies outside both reset ranges.
    music_.restoreState(cleared);
    music_.setVoice3Suppressed(false);
    auto effect = effects_.stateSnapshot();
    effect.voice3_suppressed = false; // $98, unlike $E7/$E8, is cleared.
    effects_.restoreState(effect);
    title_ = false;
    city_music_ = true; // The normal IRQ continues to call the stopped player.
    for (int reg = 0x0F; reg >= 0; --reg)
        write_sid(static_cast<std::uint8_t>(reg), 0);
}

void SceneAudio::text_tone()
{
    TextSound effect(volume_);
    for (const auto write : effect.text_tone()) write_sid(write.reg, write.value);
    volume_ = effect.volume();
}

void SceneAudio::key_click()
{
    TextSound effect(volume_);
    for (const auto write : effect.key_click()) write_sid(write.reg, write.value);
}

void SceneAudio::equipment_motor_start()
{
    for (const auto write : effects_.start(2)) write_sid(write.reg, write.value);
}

void SceneAudio::equipment_motor_stop()
{
    for (const auto write : effects_.stop()) write_sid(write.reg, write.value);
}

void SceneAudio::driving_capture_start()
{
    // State 21 calls $3D24 with Y=0. A busy voice-3 effect is deliberately a
    // no-op; the caller still records the capture independently.
    for (const auto write : effects_.start(0)) write_sid(write.reg, write.value);
}

void SceneAudio::voice3_effect_stop()
{
    for (const auto write : effects_.stop()) write_sid(write.reg, write.value);
}

void SceneAudio::voice3_effect_start(std::uint8_t effect)
{
    for (const auto write : effects_.start(effect)) write_sid(write.reg, write.value);
}

void SceneAudio::start_speech(std::uint8_t command)
{
    if (speech_active()) throw std::logic_error("Speech wrapper is already blocking");
    const auto decoded = load_speech_asset(command);
    speech_[0] = decoded;
    speech_commands_[0] = command;
    speech_count_ = 1;
    speech_index_ = 0;
    speech_event_ = 0;
    speech_cycle_ = 0;
    // $8DF2-$8DFF clears SID control registers without touching the sequencers
    // or the voice-3 effect's pointer, timer, and suppression cache.
    write_sid(0x04, 0);
    write_sid(0x0B, 0);
    write_sid(0x12, 0);
    write_sid(0x18, 7);
}

void SceneAudio::enter_city()
{
    // State16 $7ACF calls $93E2. It resets sequencer indices and durations,
    // without clearing SID registers or stopping an active voice-3 effect.
    music_.reset();
    title_ = false;
    city_music_ = true;
}

void SceneAudio::enter_ending()
{
    // $8DA6 calls $93E2 first, preserving note/command/control/instrument,
    // the volume cache and the effect-player zero-page state.  The following
    // stores at $8DA9-$8DAF stop every music channel explicitly.
    music_.reset();
    music_.stop_channels();
    title_ = false;
    // The original IRQ still calls $93F0 after this routine.  Durations of
    // $FF take the $941A/$951F path, so modulation work remains observable on
    // non-$08&3==0 IRQ phases.
    city_music_ = true;

    // $8DB1 calls $63AF, which clears the SID in descending register order.
    // Keep the order because SID writes are observable on the emulated bus.
    for (int reg = 31; reg >= 0; --reg) write_sid(static_cast<std::uint8_t>(reg), 0);
}

void SceneAudio::common_volume(game::CommonVolume action)
{
    if (action == game::CommonVolume::restore_cache) write_sid(0x18, volume_);
    else if (action == game::CommonVolume::mute) write_sid(0x18, 0);
}

void SceneAudio::begin_frame(bool common_frame, std::uint8_t flags47)
{
    if (speech_active()) return; // $8DB9 suspends the normal game IRQ.
    if (title_) {
        // PAL: $632F runs once per main-loop frame, before $63BA increments $08.
        // The title IRQ branches to $8FC0 and does NOT call the music player.
        for (const auto write : music_.tick(0, counter_, 0)) {
            write_sid(write.reg, write.value);
            if (write.reg == 0x18) volume_ = write.value;
        }
        // $62B3 detects PAL ($EA73=$FF); $6338-$6348 then skips phase 1/8.
        counter_ = ghostbusters::game::advance_pal_counter(counter_);
    } else {
        counter_ = ghostbusters::game::advance_pal_counter(counter_);
        if (city_music_) {
            music_.setVoice3Suppressed(effects_.voice3_suppressed());
            for (const auto write : music_.tick(1, counter_, flags47)) {
                write_sid(write.reg, write.value);
                if (write.reg == 0x18) volume_ = write.value;
            }
        }
        // Normal game IRQ $8FBD updates the voice-3 effect engine once per PAL frame.
        for (const auto write : effects_.tick()) write_sid(write.reg, write.value);
        // $70CE/$70D1 runs in foreground after the IRQ has returned.
        if (common_frame) write_sid(0x18, volume_);
    }
}

std::vector<float> SceneAudio::frame(bool common_frame)
{
    begin_frame(common_frame);
    return finish_frame();
}

std::vector<float> SceneAudio::finish_frame()
{
    if (speech_active()) {
        std::vector<float> pcm;
        std::uint32_t remaining = 19656;
        auto advance = [&](std::uint32_t cycles) {
            const auto part = sid_.clock(cycles);
            pcm.insert(pcm.end(), part.begin(), part.end());
            speech_cycle_ += cycles;
            remaining -= cycles;
        };
        while (remaining != 0 && speech_active()) {
            const auto& stream = speech_[speech_index_];
            // Continuous CIA timer underflows recur at latch+1 clocks. This
            // omits first-load latency and CPU/NMI reentry, not no-write gaps.
            const auto period = static_cast<std::uint32_t>(stream.timer_latch) + 1U;
            if (speech_event_ < stream.events.size()) {
                const auto& event = stream.events[speech_event_];
                const auto target = event.timer_tick * period;
                if (target - speech_cycle_ >= remaining) {
                    advance(remaining);
                } else {
                    advance(target - speech_cycle_);
                    write_sid(0x18, event.value);
                    ++speech_event_;
                }
            } else {
                const auto end = stream.total_timer_ticks * period;
                advance(std::min(remaining, end - speech_cycle_));
                if (speech_cycle_ == end) {
                    ++speech_index_;
                    speech_event_ = 0;
                    speech_cycle_ = 0;
                    write_sid(0x18, speech_active() ? 7 : volume_);
                }
            }
        }
        if (remaining) advance(remaining);
        return pcm;
    }
    // The native scheduler batches music writes at the frame boundary, then
    // renders one PAL frame (312 * 63 cycles) of SID output.
    return sid_.clock(19656);
}

} // namespace ghostbusters::audio
