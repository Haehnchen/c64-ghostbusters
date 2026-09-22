#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/drive_controls.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::DriveControls;
using ghostbusters::game::DriveControlsState;
using ghostbusters::game::DriveControlsTransition;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

DriveControlsState drive_state()
{
    DriveControlsState state;
    state.city.state3a = 0x15;
    state.distance67 = 8;
    state.vehicle_position63 = 0x40;
    state.city.sprites.x[2] = 0x80;
    // Inactive slots retain nonzero targets in the original State-20 handoff.
    state.city.sprites.target_x[0] = state.city.sprites.target_x[1] = 0x47;
    state.city.sprites.target_y[0] = state.city.sprites.target_y[1] = 0xBA;
    return state;
}

void test_scroll_speed_and_steering(const Payload& payload)
{
    auto state = drive_state();
    state.city.route_length66 = 0;
    state.distance67 = 2;
    state.scroll_position1a = 0x7E;
    state.scroll_fraction1c = 0x0F;
    state.speed1b = 0x20;
    state.animation65 = 0xFF;
    DriveControls drive(payload, state);
    (void)drive.tick({0, 0xF3, false});
    check(drive.state().scroll_position1a == 1 && drive.state().scroll_fraction1c == 0x0F &&
              drive.state().city.route_length66 == 1 && drive.state().speed1b == 0x21,
          "scroll wrap advances route and preserves fractional nibble");
    check(drive.state().city.sprites.y[5] == 1 && drive.state().city.sprites.y[7] == 1 &&
              drive.state().city.sprites.y[4] == 0x80 &&
              drive.state().city.sprites.y[6] == 0x80,
          "road sprite pairs follow the scroll position");
    check(drive.state().direction64 == 1 && drive.state().animation65 == 0x80 &&
              drive.state().vehicle_position63 == 0x40 &&
              drive.state().city.sprites.multicolor_mask == 0xF8,
          "right wins simultaneous steering while common-frame position stays external");

    state = drive_state();
    DriveControls left(payload, state);
    (void)left.tick({0, 0xFB, false});
    check(left.state().direction64 == 0xFF, "active-low left steering sets minus one");

    for (std::uint8_t vehicle = 0; vehicle < 4; ++vehicle) {
        state = drive_state();
        state.vehicle5c = vehicle;
        state.speed1b = static_cast<std::uint8_t>(0x60 + vehicle * 0x10);
        if (vehicle == 3) state.speed1b = 0xA0;
        DriveControls capped(payload, state);
        (void)capped.tick({});
        const std::uint8_t expected[] = {0x60, 0x70, 0x80, 0xA0};
        check(capped.state().speed1b == expected[vehicle], "vehicle speed cap");
    }
}

void test_spawn_order_and_random_transform(const Payload& payload)
{
    auto state = drive_state();
    state.city.route_length66 = 1;
    state.city.roamer_counters6f[3] = 1;
    state.city.roamer_counters6f[2] = 1;
    state.city.sprites.y_expand_mask = 0xF0;
    DriveControls drive(payload, state);
    (void)drive.tick({0x80, 0xFF, false});
    check(drive.state().roamer_source73[0] == 3 && drive.state().roamer_source73[1] == 2 &&
              drive.state().city.roamer_counters6f[3] == 0 &&
              drive.state().city.roamer_counters6f[2] == 0,
          "descending source scan fills ghost slots in order");
    check(drive.state().city.sprites.x[0] == 0x11 &&
              drive.state().city.sprites.pointers[0] == 9 &&
              drive.state().city.sprites.x[1] == 0x12 &&
              drive.state().city.sprites.pointers[1] == 8 &&
              drive.state().city.sprites.target_y[0] == 0xF0 &&
              drive.state().city.sprites.target_y[1] == 0xF0,
          "each assignment rotates the caller-supplied random byte");
    check(drive.state().city.sprites.x_expand_mask == 0x03 &&
              drive.state().city.sprites.y_expand_mask == 0xF3,
          "spawn enables the two ghost expansion bits");
}

void test_vacuum_edges_and_capture_audio(const Payload& payload)
{
    auto state = drive_state();
    state.city.owned_mask6d = 0x20;
    state.city.fire_latch11 = 0x10;
    state.city.sprites.x[0] = 0x70;
    state.city.sprites.y[0] = 0x40;
    state.city.sprites.target_x[0] = 0x70;
    state.city.sprites.target_y[0] = 0xF0;
    DriveControls vacuum(payload, state);
    (void)vacuum.tick({0, 0xEF, false});
    check(vacuum.state().city.fire_latch11 == 0 &&
              vacuum.state().city.sprites.target_x[0] == 0x7C &&
              vacuum.state().city.sprites.target_y[0] == 0x78,
          "new fire press with ghost trap equipment acquires an in-range ghost");

    state = drive_state();
    state.city.sprites.x[0] = state.city.sprites.target_x[0] = 0x7C;
    state.city.sprites.y[0] = state.city.sprites.target_y[0] = 0x78;
    DriveControls capture(payload, state);
    const auto started = capture.tick({0, 0xFF, false});
    check(capture.state().capture_timer75[0] == 0x1F && started.effect0_calls == 1 &&
              started.started_effects.size() == 1 && started.started_effects[0] == 0,
          "arrival starts and immediately advances a 32-tick capture with effect zero");

    DriveControls busy(payload, state);
    const auto ignored = busy.tick({0, 0xFF, true});
    check(ignored.effect0_calls == 1 && ignored.started_effects.empty(),
          "busy voice-3 player ignores the proven effect-zero request");
}

void test_capture_expiry_is_state21_shadow_only(const Payload& payload)
{
    auto state = drive_state();
    state.capture_timer75[0] = 1;
    state.roamer_source73[0] = 2;
    state.city.sprites.x[0] = state.city.sprites.target_x[0] = 0x7C;
    state.city.sprites.y[0] = state.city.sprites.target_y[0] = 0x78;
    state.city.sprites.x[6] = 0x91;
    state.city.sprites.y[6] = 0x92;
    state.city.sprites.target_x[6] = 0x93;
    state.city.sprites.target_y[6] = 0x94;
    state.city.sprite_control_c0[4] = 1;
    state.city.sprite_control_c0[6] = 1;
    DriveControls drive(payload, state);
    (void)drive.tick({});
    check(drive.state().capture_timer75[0] == 0 && drive.state().city.sprites.y[0] == 0 &&
              drive.state().city.active_roamer_count28 == 2,
          "capture expiry clears the drive ghost and recounts city roamers");
    check(drive.state().city.sprites.x[6] == 0x91 && drive.state().city.sprites.y[6] == 0x7F &&
              drive.state().city.sprites.target_x[6] == 0x93 &&
              drive.state().city.sprites.target_y[6] == 0x94,
          "$9851 in State 21 does not copy its payload defaults into active city coordinates");
    check(drive.state().city.shadow_x_ea46[6] == 0x00 &&
              drive.state().city.shadow_y_ea47[6] == 0xE2 &&
              drive.state().city.shadow_target_x_ea56[6] == 0x62 &&
              drive.state().city.shadow_target_y_ea57[6] == 0x7A &&
              drive.state().city.sprites.pointers[0] == 8,
          "$9851 still restores the State-21 shadow quartet and returns target Y to animation");
}

void test_transition_and_non_state_gate(const Payload& payload)
{
    auto state = drive_state();
    state.city.route_length66 = state.distance67;
    state.speed1b = 1;
    state.city.key17 = 0x42;
    DriveControls arrived(payload, state);
    const auto result = arrived.tick({});
    check(arrived.transition() == DriveControlsTransition::building &&
              arrived.state().speed1b == 0 && arrived.state().city.key17 == 0 &&
              result.effect0_calls == 0,
          "final deceleration tick clears key and advances to State 22 without audio");

    state = drive_state();
    state.city.state3a = 0x12;
    state.city.sprites.multicolor_mask = 0x35;
    DriveControls gated(payload, state);
    (void)gated.tick({});
    check(gated.state().city.sprites.multicolor_mask == 0x35,
          "non-State-21 call has no handler side effects");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_scroll_speed_and_steering(payload);
        test_spawn_order_and_random_transform(payload);
        test_vacuum_edges_and_capture_audio(payload);
        test_capture_expiry_is_state21_shadow_only(payload);
        test_transition_and_non_state_gate(payload);
        std::cout << "native drive-controls tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
