#include "assets/payload.hpp"
#include "audio/scene_audio.hpp"
#include "game/drive_controls.hpp"
#include "game/pal_counter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

double pcm_difference(const std::vector<float>& lhs, const std::vector<float>& rhs)
{
    if (lhs.size() != rhs.size()) throw std::runtime_error("PCM frame sizes differ");
    double difference = 0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (!std::isfinite(lhs[index]) || !std::isfinite(rhs[index]))
            throw std::runtime_error("Nonfinite driving PCM");
        difference += std::abs(static_cast<double>(lhs[index]) - rhs[index]);
    }
    return difference;
}

double pcm_energy(const std::vector<float>& pcm)
{
    double energy = 0;
    for (const auto value : pcm) {
        if (!std::isfinite(value)) throw std::runtime_error("Nonfinite driving PCM");
        energy += std::abs(static_cast<double>(value));
    }
    return energy;
}

void test_ending_audio_reset(const ghostbusters::assets::Payload& payload)
{
    using ghostbusters::audio::SceneAudio;

    SceneAudio active(payload, SceneAudio::StartPoint::title_music);
    active.leave_title();
    active.enter_city();
    for (unsigned frame = 0; frame < 8; ++frame) static_cast<void>(active.frame());

    // Exercise both stateful audio paths before the ending wrapper.  The
    // text tone changes EA82, while effect 3 leaves a live decoder that the
    // original $8D96 routine must preserve.
    active.text_tone();
    active.voice3_effect_start(3);
    const auto music_before = active.music_state();
    const auto effect_before = active.effect_state();
    const auto counter_before = active.irq_counter();

    active.enter_ending();
    const auto music_after = active.music_state();
    if (music_after.sequence_index != std::array<std::uint8_t, 3>{} ||
        music_after.phrase_index != std::array<std::uint8_t, 3>{} ||
        music_after.duration != std::array<std::uint8_t, 3>{0xFF, 0xFF, 0xFF})
        throw std::runtime_error("Ending did not reset and stop all music channels");
    if (music_after.note != music_before.note ||
        music_after.command != music_before.command ||
        music_after.control != music_before.control ||
        music_after.instrument != music_before.instrument ||
        music_after.volume != music_before.volume)
        throw std::runtime_error("Ending changed preserved music caches");
    if (active.effect_state() != effect_before)
        throw std::runtime_error("Ending changed the voice-3 effect decoder state");

    // The caller's ending frame uses common_frame=false, matching the
    // original state >=42 path.  It still advances the PAL IRQ and ticks the
    // preserved effect, while stopped music remains at $FF durations.
    const auto ending_state = active.music_state();
    const auto ending_effect = active.frame(false);
    if (ending_effect.empty()) throw std::runtime_error("Ending lost its PAL audio clock");
    if (active.irq_counter() != ghostbusters::game::advance_pal_counter(counter_before))
        throw std::runtime_error("Ending IRQ did not advance with original PAL semantics");
    const auto music_after_irq = active.music_state();
    if (music_after_irq.sequence_index != ending_state.sequence_index ||
        music_after_irq.phrase_index != ending_state.phrase_index ||
        music_after_irq.duration != ending_state.duration)
        throw std::runtime_error("Stopped ending music cursors advanced on the next IRQ");
    if (active.effect_state() == effect_before)
        throw std::runtime_error("Preserved ending effect did not receive the next IRQ tick");

    // A text tone must not survive the complete D41F..D400 clear.  Compare
    // against the same city/ending path without the tone after synchronized
    // frame clocks; both SID outputs should then be silent and equal.
    SceneAudio toned(payload, SceneAudio::StartPoint::title_music);
    SceneAudio clean(payload, SceneAudio::StartPoint::title_music);
    toned.leave_title(); clean.leave_title();
    toned.enter_city(); clean.enter_city();
    for (unsigned frame = 0; frame < 8; ++frame) {
        static_cast<void>(toned.frame());
        static_cast<void>(clean.frame());
    }
    toned.text_tone();
    toned.enter_ending();
    clean.enter_ending();
    const auto toned_pcm = toned.frame(false);
    const auto clean_pcm = clean.frame(false);
    if (pcm_difference(toned_pcm, clean_pcm) > 0.01)
        throw std::runtime_error("Ending SID clear did not remove the text tone");
}

void test_foreground_audio_order(const ghostbusters::assets::Payload& payload)
{
    using ghostbusters::audio::SceneAudio;
    using ghostbusters::audio::SidWrite;
    SceneAudio audio(payload, SceneAudio::StartPoint::title_music);
    audio.leave_title();
    audio.enter_city();
    audio.enable_sid_trace();
    (void)audio.frame(); // $08: 0 -> 2; trigger on the following frame.
    (void)audio.take_sid_writes();
    audio.begin_frame();
    if (audio.irq_counter() != 3)
        throw std::runtime_error("Foreground received the previous IRQ counter");
    audio.driving_capture_start();
    const auto started = audio.effect_state();
    const std::vector<SidWrite> expected_start{
        {0x18, 0xFF}, {0x13, 0}, {0x14, 0}, {0x11, 0}, {0x10, 0}, {0x12, 0}};
    if (audio.take_sid_writes() != expected_start || started.timer != 1)
        throw std::runtime_error("Effect start did not follow common cached-volume restore");
    (void)audio.finish_frame();
    if (audio.irq_counter() != 3 || audio.effect_state() != started ||
        !audio.take_sid_writes().empty())
        throw std::runtime_error("Foreground effect advanced before the next IRQ");

    audio.begin_frame();
    const auto next = audio.take_sid_writes();
    if (audio.irq_counter() != 4 || audio.effect_state() == started ||
        next.empty() || next.back() != SidWrite{0x18, 3})
        throw std::runtime_error("Next IRQ did not tick music/effect before restoring volume");
    (void)audio.finish_frame();

    // Crossed-beam/ending waits still run the IRQ but omit foreground D418.
    audio.begin_frame(false);
    const auto suspended = audio.take_sid_writes();
    if (audio.irq_counter() != 5 ||
        std::any_of(suspended.begin(), suspended.end(), [](auto write) { return write.reg == 0x18; }))
        throw std::runtime_error("Suspended foreground restored cached volume");
    (void)audio.finish_frame();

    // Speech starts after this frame's IRQ. Its PCM must not erase that tick,
    // and subsequent speech-only frames must not run another normal IRQ.
    audio.begin_frame();
    const auto before_speech = audio.irq_counter();
    const auto effect_before_speech = audio.effect_state();
    audio.start_speech(1);
    if (audio.speech_command() != 1) throw std::runtime_error("Wrong gameplay speech identity");
    (void)audio.finish_frame();
    (void)audio.frame(false);
    if (audio.irq_counter() != before_speech || audio.effect_state() != effect_before_speech)
        throw std::runtime_error("Speech changed the normal IRQ/effect clock");
}

} // namespace

int main()
{
    try {
        constexpr std::array<std::uint8_t, 12> expected_counter{
            2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14};
        std::uint8_t counter = 0;
        for (const auto expected : expected_counter) {
            counter = ghostbusters::game::advance_pal_counter(counter);
            if (counter != expected) throw std::runtime_error("Incorrect PAL counter phase");
        }
        const auto payload = ghostbusters::assets::Payload::embedded();
        ghostbusters::audio::SceneAudio audio(payload);
        std::size_t samples = 0;
        unsigned frames = 0;
        float minimum = 0, maximum = 0;
        bool saw_first = false, saw_second = false;
        while (audio.speech_active() && frames < 300) {
            saw_first |= audio.speech_command() == 1;
            saw_second |= audio.speech_command() == 3;
            const auto pcm = audio.frame();
            if (pcm.empty()) throw std::runtime_error("Startup lost its frame PCM");
            for (const auto value : pcm) {
                if (!std::isfinite(value)) throw std::runtime_error("Nonfinite startup PCM");
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
            samples += pcm.size();
            ++frames;
        }
        // Both commands, including silent timer gaps, must occupy about 4.25s.
        if (!saw_first || !saw_second || audio.speech_command() != -1 ||
            audio.speech_active() || frames < 210 || frames > 215 ||
            samples < 200000 || samples > 207000 || maximum - minimum < 0.01F)
            throw std::runtime_error("Incomplete or silent startup speech sequence");
        if (audio.frame().empty() || audio.speech_active())
            throw std::runtime_error("Startup did not hand off to title music");
        audio.leave_title();
        audio.text_tone();
        if (audio.frame().empty()) throw std::runtime_error("Dialog audio lost its clock");
        ghostbusters::audio::SceneAudio motor(payload, ghostbusters::audio::SceneAudio::StartPoint::title_music);
        ghostbusters::audio::SceneAudio quiet(payload, ghostbusters::audio::SceneAudio::StartPoint::title_music);
        motor.leave_title(); quiet.leave_title();
        // The real path prints the franchise dialog before equipment. That
        // sets the cached D418 volume to $0F (startup's $FF disables voice 3).
        motor.text_tone(); quiet.text_tone();
        quiet.equipment_motor_stop();
        double difference = 0;
        for (unsigned frame = 0; frame < 64; ++frame) {
            motor.equipment_motor_start();
            const auto active = motor.frame();
            const auto silent = quiet.frame();
            if (active.size() != silent.size()) throw std::runtime_error("Effect changed the PAL sample budget");
            for (unsigned i = 0; i < active.size(); ++i) {
                if (!std::isfinite(active[i])) throw std::runtime_error("Nonfinite motor PCM");
                difference += std::abs(active[i] - silent[i]);
            }
        }
        if (difference < 1.0) throw std::runtime_error("Equipment effect is not reaching the SID output");
        motor.equipment_motor_stop();
        if (motor.frame().empty()) throw std::runtime_error("Stopping effect lost the PCM clock");
        (void)quiet.frame(); // Keep the reference SID at the same resampler phase.
        motor.enter_city();
        if (!motor.city_music())
            throw std::runtime_error("Entering the city did not enable city music");
        difference = 0;
        for (unsigned frame = 0; frame < 64; ++frame) {
            const auto music = motor.frame();
            const auto background = quiet.frame();
            if (music.size() != background.size()) throw std::runtime_error("City music changed PAL sample budget");
            for (unsigned i = 0; i < music.size(); ++i) {
                if (!std::isfinite(music[i])) throw std::runtime_error("Nonfinite city music PCM");
                difference += std::abs(music[i] - background[i]);
            }
        }
        if (difference < 1.0) throw std::runtime_error("City music did not resume after equipment");

        ghostbusters::audio::SceneAudio driving(
            payload, ghostbusters::audio::SceneAudio::StartPoint::title_music);
        ghostbusters::audio::SceneAudio city_only(
            payload, ghostbusters::audio::SceneAudio::StartPoint::title_music);
        driving.leave_title();
        city_only.leave_title();
        driving.enter_city();
        city_only.enter_city();

        ghostbusters::game::DriveControlsState capture_state;
        capture_state.city.state3a = 0x15;
        capture_state.distance67 = 8;
        capture_state.vehicle_position63 = 0x40;
        capture_state.city.sprites.x[2] = 0x80;
        capture_state.city.sprites.target_x[0] = capture_state.city.sprites.target_x[1] = 0x47;
        capture_state.city.sprites.target_y[0] = capture_state.city.sprites.target_y[1] = 0xBA;
        capture_state.city.sprites.x[0] = capture_state.city.sprites.target_x[0] = 0x7C;
        capture_state.city.sprites.y[0] = capture_state.city.sprites.target_y[0] = 0x78;
        ghostbusters::game::DriveControls controls(payload, capture_state);
        const auto capture = controls.tick({0, 0xFF, driving.effect_busy()});
        if (capture.effect0_calls != 1 || capture.started_effects != std::vector<std::uint8_t>{0})
            throw std::runtime_error("DriveControls capture event was not accepted");
        for (const auto effect : capture.started_effects) {
            if (effect != 0) throw std::runtime_error("Unexpected driving effect index");
            driving.driving_capture_start();
        }
        if (!driving.effect_busy()) throw std::runtime_error("Driving effect did not start");

        double driving_difference = 0;
        double city_music_energy = 0;
        auto active = driving.frame();
        auto background = city_only.frame();
        driving_difference += pcm_difference(active, background);
        // A second capture request while voice 3 is busy must not restart the
        // stream. The natural end below therefore remains at tick 37.
        driving.driving_capture_start();
        if (!driving.effect_busy()) throw std::runtime_error("Busy driving effect was cleared");
        for (unsigned tick = 1; tick < 36; ++tick) {
            active = driving.frame();
            background = city_only.frame();
            if (!driving.effect_busy())
                throw std::runtime_error("Driving effect ended before its terminator");
            driving_difference += pcm_difference(active, background);
        }
        active = driving.frame();
        background = city_only.frame();
        driving_difference += pcm_difference(active, background);
        if (driving.effect_busy())
            throw std::runtime_error("Driving effect did not naturally terminate");
        if (driving_difference < 1.0)
            throw std::runtime_error("Driving effect is not reaching the SID output");

        // Effect 0 does not stop city music at its terminator. Subsequent
        // frames must still clock the resumed music path and produce PCM.
        for (unsigned frame = 0; frame < 8; ++frame) {
            active = driving.frame();
            background = city_only.frame();
            city_music_energy += pcm_energy(active);
            (void)pcm_difference(active, background);
        }
        if (city_music_energy < 1.0)
            throw std::runtime_error("City music did not continue after driving effect");

        test_ending_audio_reset(payload);
        test_foreground_audio_order(payload);

        std::cout << "Startup speech/PCM/music transition passed (serial decoder timing)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
