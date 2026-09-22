#pragma once

#include "audio/music_player.hpp"
#include "audio/effect_player.hpp"
#include "audio/sid_chip.hpp"
#include "audio/speech_assets.hpp"
#include "game/input_frame.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::audio {

// Native scene audio. Speech uses embedded, pre-exported event streams and
// never executes source playback instructions at runtime.
class SceneAudio {
public:
    enum class StartPoint { startup, title_music };
    explicit SceneAudio(const assets::Payload& payload, StartPoint start = StartPoint::startup);
    void leave_title();
    // The native title timeline also advances during its blocking speech.
    void synchronize_title_counter(std::uint8_t counter);
    // Stop/reset music and release voice-3 suppression, retaining effect decoder
    // state and cached volume. Clear only SID registers 0x00..0x0F.
    // Shared by full and partial resets.
    void reset_foreground();
    void text_tone();
    void key_click();
    void equipment_motor_start();
    void equipment_motor_stop();
    // State 21 requests voice-3 effect 0 when a ghost reaches its target.
    // EffectPlayer ignores this request while another voice-3 effect is busy.
    void driving_capture_start();
    // Ordered primitives for state handlers that explicitly stop/start voice 3.
    void voice3_effect_stop();
    void voice3_effect_start(std::uint8_t effect);
    // Speech blocks foreground and regular music/effect ticks until
    // speech_active() clears; their decoder state survives playback.
    void start_speech(std::uint8_t command);
    void enter_city();
    // Ending setup resets music cursors, stops all three music channels, and
    // clears every SID register. The volume cache and voice-3 effect decoder
    // intentionally survive this transition.
    void enter_ending();
    // A suspended foreground still runs the IRQ music/effect tick, but does
    // not perform the common-frame cached-volume write.
    [[nodiscard]] std::vector<float> frame(bool common_frame = true);
    // Live order: begin_frame(false, previous flags), post-poll common_volume,
    // foreground events, finish_frame (PCM). The default common_frame=true
    // retains the ordinary unpaused path for isolated scene diagnostics.
    // A newly started effect first ticks in the NEXT IRQ.
    void begin_frame(bool common_frame = true, std::uint8_t flags47 = 0);
    // Apply the post-poll foreground decision without changing
    // the cached volume. IRQ work has already seen the previous flags47.
    void common_volume(game::CommonVolume action);
    [[nodiscard]] std::vector<float> finish_frame();
    // Opt-in ordered SID bus trace for diagnostics; disabled in gameplay.
    void enable_sid_trace(bool enabled = true) noexcept { trace_enabled_ = enabled; }
    [[nodiscard]] std::vector<SidWrite> take_sid_writes();
    [[nodiscard]] bool speech_active() const noexcept { return speech_index_ < speech_count_; }
    // Read-only diagnostic identity, including the two startup clips.
    [[nodiscard]] int speech_command() const noexcept
    { return speech_active() ? speech_commands_[speech_index_] : -1; }
    [[nodiscard]] bool effect_busy() const noexcept { return effects_.active(); }
    [[nodiscard]] MusicPlayerSnapshot music_state() const noexcept { return music_.stateSnapshot(); }
    [[nodiscard]] EffectPlayerSnapshot effect_state() const noexcept { return effects_.stateSnapshot(); }
    [[nodiscard]] std::uint8_t irq_counter() const noexcept { return counter_; }
    [[nodiscard]] bool city_music() const noexcept { return city_music_; }

private:
    void write_sid(std::uint8_t reg, std::uint8_t value);
    bool trace_enabled_ = false;
    std::vector<SidWrite> sid_trace_;
    MusicPlayer music_;
    EffectPlayer effects_;
    SidChip sid_;
    std::uint8_t counter_ = 0;
    std::uint8_t volume_ = 0;
    bool title_ = true;
    bool city_music_ = false;
    std::array<DecodedSpeech, 2> speech_;
    std::array<std::uint8_t, 2> speech_commands_{1, 3};
    std::size_t speech_index_ = 2;
    std::size_t speech_count_ = 2;
    std::size_t speech_event_ = 0;
    std::uint32_t speech_cycle_ = 0;
};

} // namespace ghostbusters::audio
