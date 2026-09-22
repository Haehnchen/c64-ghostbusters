#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "audio/scene_audio.hpp"
#include "game/drive_controls.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::audio::SceneAudio;
using ghostbusters::game::DriveControls;
using ghostbusters::game::DriveControlsState;
using ghostbusters::game::DriveControlsTransition;

constexpr std::uint8_t kFireMask = 0x10;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

DriveControlsState drive_state(bool owns_vacuum)
{
    DriveControlsState state;
    state.city.state3a = 0x15;
    state.distance67 = 0xFF;
    state.city.route_length66 = 0;
    state.vehicle_position63 = 0x40;
    state.city.owned_mask6d = owns_vacuum ? 0x20 : 0;
    state.city.sprites.x[2] = 0x80;
    state.city.sprites.x[0] = state.city.sprites.target_x[0] = 0x70;
    state.city.sprites.y[0] = 0x40;
    state.city.sprites.target_y[0] = 0xF0;
    // The inactive second slot retains its nonzero State-20 target.
    state.city.sprites.target_x[1] = 0x47;
    state.city.sprites.target_y[1] = 0xBA;
    state.city.fire_latch11 = kFireMask;
    return state;
}

void test_active_low_space_press_reaches_drive_and_audio(const Payload& payload)
{
    SceneAudio audio(payload, SceneAudio::StartPoint::title_music);
    audio.leave_title();
    audio.enter_city();

    DriveControls drive(payload, drive_state(true));

    std::size_t effect0_started = 0;
    std::size_t effect0_calls = 0;
    std::size_t pcm_samples = 0;
    bool acquisition_target_seen = false;
    bool capture_expired = false;

    for (unsigned tick = 0; tick < 256; ++tick) {
        // SDL's Space mapping reaches the original $33 joystick byte with
        // fire active-low: $EF on the press edge and $FF after release.
        const auto joystick = tick == 0 ? static_cast<std::uint8_t>(0xFFU & ~kFireMask)
                                        : static_cast<std::uint8_t>(0xFF);
        const auto result = drive.tick({0, joystick, audio.effect_busy()});
        effect0_calls += result.effect0_calls;
        for (const auto effect : result.started_effects) {
            check(effect == 0, "drive capture must request voice-3 effect zero");
            ++effect0_started;
            audio.driving_capture_start();
        }

        const auto pcm = audio.frame();
        check(!pcm.empty(), "city music/capture frame must produce PCM");
        pcm_samples += pcm.size();
        for (const auto sample : pcm) {
            check(std::isfinite(sample), "city music/capture PCM must be finite");
        }

        if (drive.state().city.sprites.target_x[0] == 0x7C &&
            drive.state().city.sprites.target_y[0] == 0x78) {
            acquisition_target_seen = true;
        }
        if (acquisition_target_seen && drive.state().capture_timer75[0] == 0 &&
            drive.state().city.sprites.y[0] == 0) {
            capture_expired = true;
            break;
        }
    }

    check(acquisition_target_seen,
          "an active-low Space press with the purchased vacuum must acquire the in-range ghost");
    check(effect0_calls == 1 && effect0_started == 1,
          "exactly one capture request and effect-zero start must occur");
    check(capture_expired && drive.state().capture_timer75[0] == 0 &&
              drive.state().city.sprites.y[0] == 0,
          "capture expiry must remove the ghost from the active drive slot");
    check(pcm_samples != 0, "the integrated city/effect audio path must emit PCM");
    check(drive.transition() == DriveControlsTransition::driving,
          "the bounded capture replay must remain in the driving state");
}

void test_active_low_space_without_vacuum_flows_away(const Payload& payload)
{
    DriveControls drive(payload, drive_state(false));

    bool target_unchanged = false;
    bool ghost_left_drive = false;
    for (unsigned tick = 0; tick < 256; ++tick) {
        const auto joystick = tick == 0 ? static_cast<std::uint8_t>(0xFFU & ~kFireMask)
                                        : static_cast<std::uint8_t>(0xFF);
        const auto result = drive.tick({0, joystick, false});
        check(result.effect0_calls == 0 && result.started_effects.empty(),
              "a missing vacuum must never start capture effect zero");

        if (tick == 0) {
            target_unchanged = drive.state().city.sprites.target_x[0] == 0x70 &&
                               drive.state().city.sprites.target_y[0] == 0xF0 &&
                               drive.state().capture_timer75[0] == 0;
        }
        if (drive.state().city.sprites.y[0] == 0) {
            ghost_left_drive = true;
            break;
        }
    }

    check(target_unchanged,
          "without the vacuum the tap must leave the free ghost target unchanged");
    check(ghost_left_drive && drive.state().capture_timer75[0] == 0 &&
              drive.state().city.sprites.target_y[0] == 0xF0,
          "a free ghost must leave the lower boundary with its F0 target retained");
    check(drive.transition() == DriveControlsTransition::driving,
          "the free-ghost replay must remain in the driving state");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_active_low_space_press_reaches_drive_and_audio(payload);
        test_active_low_space_without_vacuum_flows_away(payload);
        std::cout << "drive keyboard integration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
