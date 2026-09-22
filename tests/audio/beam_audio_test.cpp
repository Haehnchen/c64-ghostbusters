#include "assets/payload.hpp"
#include "audio/scene_audio.hpp"
#include "game/beam_controls.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ghostbusters;

static game::DriveControlsState fixture(bool crossed)
{
    game::DriveControlsState s;
    s.city.state3a = 27;
    s.city.fire_latch11 = 0x10;
    s.city.backpack_charge3e = 0x99;
    auto& p = s.city.sprites;
    p.x[5] = p.target_x[5] = 0x50;
    p.x[6] = p.target_x[6] = 0x78;
    p.y[5] = p.target_y[5] = p.y[6] = p.target_y[6] = 0xB0;
    p.pointers[5] = 0x16;
    p.pointers[6] = crossed ? 0x17 : 0x16;
    return s;
}

static void apply(audio::SceneAudio& audio, const game::BeamControlsTickResult& result)
{
    for (const auto event : result.audio_events) {
        if (event.action == game::BeamAudioAction::voice3_stop) audio.voice3_effect_stop();
        else audio.voice3_effect_start(event.effect);
    }
}

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        audio::SceneAudio active(payload, audio::SceneAudio::StartPoint::title_music);
        audio::SceneAudio background(payload, audio::SceneAudio::StartPoint::title_music);
        active.leave_title(); background.leave_title();
        active.enter_city(); background.enter_city();
        active.voice3_effect_start(2);
        (void)active.frame(); (void)background.frame();
        game::BeamControls beam(payload, fixture(false), {0,0,{0,0},0,{0,0}}, {0,0,0});
        const auto end = beam.tick({1,1,0xEF});
        if (end.audio_events != std::vector<game::BeamAudioEvent>{
                {game::BeamAudioAction::voice3_stop,0}, {game::BeamAudioAction::voice3_start,3}})
            throw std::runtime_error("Beam completion lost ordered stop/start");
        apply(active, end);
        audio::EffectPlayer expected(payload);
        (void)expected.start(3);
        double difference = 0;
        for (unsigned tick = 0; tick < 100; ++tick) {
            (void)expected.tick();
            const auto pcm = active.frame();
            const auto ambient = background.frame();
            if (active.effect_busy() != expected.active() || pcm.size() != ambient.size())
                throw std::runtime_error("Beam effect did not replace busy motor or lost PAL clock");
            for (unsigned i=0; i<pcm.size(); ++i) {
                if (!std::isfinite(pcm[i])) throw std::runtime_error("Nonfinite beam PCM");
                difference += std::abs(double(pcm[i]) - ambient[i]);
            }
        }
        if (difference < 1 || active.effect_busy()) throw std::runtime_error("Beam effect missing or unterminated");

        game::BeamControls crossed(payload, fixture(true), {0,0,{0,0},0,{0,0}}, {0,0,0});
        active.driving_capture_start();
        apply(active, crossed.tick({1,1,0xFF}));
        if (crossed.pending_irqs() != 64) throw std::runtime_error("Missing crossed-stream wait");
        double energy = 0;
        for (unsigned irq=0; irq<64; ++irq) {
            // Same order as the live wait: IRQ audio, then foreground helper.
            const auto pcm = active.frame(false);
            for (const auto sample : pcm) {
                if (!std::isfinite(sample)) throw std::runtime_error("Nonfinite suspended PCM");
                energy += std::abs(double(sample));
            }
            apply(active, crossed.resume_irq());
        }
        if (energy < 1 || active.effect_busy() || crossed.state().city.state3a != 29)
            throw std::runtime_error("Crossing wait lost audio or failed final stop");
        std::cout << "Beam audio and suspended IRQ output passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
