#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/catch_sequence.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::BeamControlsRegisters;
using ghostbusters::game::BuildingControlsRegisters;
using ghostbusters::game::CatchAudioAction;
using ghostbusters::game::CatchAudioEvent;
using ghostbusters::game::CatchSequence;
using ghostbusters::game::CatchSequenceRegisters;
using ghostbusters::game::DriveControlsState;

namespace {

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

DriveControlsState catch_state(std::uint8_t state)
{
    DriveControlsState value;
    value.city.state3a = state;
    value.city.key17 = 0x41;
    auto& sprites = value.city.sprites;
    for (std::size_t i = 0; i < 8; ++i) {
        sprites.x[i] = sprites.target_x[i] = static_cast<std::uint8_t>(0x40 + i * 4);
        sprites.y[i] = sprites.target_y[i] = static_cast<std::uint8_t>(0x60 + i * 3);
        sprites.pointers[i] = 0x0E;
    }
    return value;
}

BuildingControlsRegisters building_registers()
{
    return {0, 0, {0, 1}, 2, {0, 0}};
}

void test_storage_capacity(const Payload& payload)
{
    for (const auto owned : {0, 0x40}) {
        for (const auto stored : {9, 10}) {
            auto state = catch_state(31);
            state.city.owned_mask6d = owned;
            state.city.empty_traps6b = 2;
            state.city.current_building6e = 7;
            state.city.building_status_c8[7] = 0xBF;
            state.city.sprites.pointers[0] = 0x25;
            const bool use_storage = owned == 0x40 && stored < 10;
            CatchSequence capture(payload, state, building_registers(), {0, 0, 0},
                                  {2, static_cast<std::uint8_t>(stored), 0});
            (void)capture.tick();
            check(capture.registers().full_traps6c == stored + int(use_storage) &&
                      capture.state().city.empty_traps6b == 2 - int(!use_storage),
                  "laser stores the tenth ghost, then uses a trap; no laser always uses a trap");
            check(capture.state().city.balance57.byte58 == 5 &&
                      capture.state().city.building_status_c8[7] == 0,
                  "storage choice preserves the normal capture award and haunting clearance");
            (void)capture.tick();
            check(capture.registers().full_traps6c == stored + int(use_storage) &&
                      capture.state().city.empty_traps6b == 2 - int(!use_storage) &&
                      capture.state().city.balance57.byte58 == 5,
                  "closing animation does not store, consume or award a second time");
        }
    }
}

void test_state28_paths(const Payload& payload)
{
    auto missed = catch_state(0x1C);
    missed.city.sprites.x[5] = missed.city.sprites.target_x[5] = 0x52;
    missed.city.sprites.y[5] = missed.city.sprites.target_y[5] = 0x91;
    CatchSequence miss(payload, missed, building_registers(), {0, 0x9C, 0}, {4, 2, 3});
    (void)miss.tick({0, 1});
    check(miss.state().city.state3a == 0x1D && miss.state().city.key17 == 0,
          "$86D5 advances a threshold miss to State 29 through $8D86");
    check(miss.state().city.sprites.target_x[4] == 0x52 &&
              miss.state().city.sprites.target_y[4] == 0x91 &&
              miss.beam_registers().post_failure_ea7a == 1,
          "State 28 targets the random selected buster and arms failure speech");
    check(miss.state().city.sprites.pointers[0] == 0 &&
              miss.state().city.sprites.pointers[3] == 0,
          "State 28 removes all four deformation sprites on threshold failure");

    auto caught = catch_state(0x1C);
    auto& sprites = caught.city.sprites;
    sprites.x[7] = sprites.target_x[7] = 0x60;
    sprites.y[7] = sprites.target_y[7] = 0x80;
    sprites.x[4] = sprites.target_x[4] = 0x60;
    sprites.y[4] = sprites.target_y[4] = 0x50;
    CatchSequence hit(payload, caught, building_registers(), {0, 0, 0}, {0, 0, 0});
    (void)hit.tick({0, 1});
    check(hit.state().city.state3a == 0x1E &&
              hit.beam_registers().beam_scratch7a == 3,
          "both half-open deformation intervals hand State 28 directly to State 30");
}

void test_state29_failure_and_speech(const Payload& payload)
{
    auto collision = catch_state(0x1D);
    collision.city.backup_men3d = 3;
    collision.city.sprites.x[4] = collision.city.sprites.target_x[4] = 0x60;
    collision.city.sprites.y[4] = collision.city.sprites.target_y[4] = 0x70;
    for (std::size_t buster = 5; buster <= 6; ++buster) {
        collision.city.sprites.target_x[buster] = 0x60;
        collision.city.sprites.target_y[buster] = 0x70;
    }
    CatchSequence one_hit(payload, collision, building_registers(), {0, 0, 0}, {0, 0, 0});
    (void)one_hit.tick();
    check(one_hit.state().city.backup_men3d == 2 &&
              one_hit.state().city.sprites.pointers[6] == 0x18 &&
              one_hit.state().city.sprites.pointers[5] != 0x18,
          "$8727 exits after the first buster collision even for identical targets");

    auto state = catch_state(0x1D);
    state.city.current_building6e = 3;
    state.city.building_status_c8[3] = 0x9C;
    state.city.sprites.x[4] = state.city.sprites.target_x[4] = 0;
    state.city.sprites.y[4] = state.city.sprites.target_y[4] = 0x50;
    CatchSequence sequence(payload, state, building_registers(), {0, 0, 1}, {0, 0, 0});
    const auto result = sequence.tick({7, 0});
    check(result.blocking_request && result.audio_events ==
              std::vector<CatchAudioEvent>{{CatchAudioAction::speech_start, 2}} &&
              sequence.state().city.state3a == 0x1D,
          "State 29 requests command 2 and remains suspended at the blocking JSR");
    check(sequence.state().city.building_status_c8[3] == 0 &&
              sequence.state().city.pk_high5b == 0x03,
          "State 29 applies building cleanup and packed-BCD PK increase before speech");
    check(sequence.tick().audio_events.empty() && sequence.speech_blocked(),
          "a suspended handler does not emit duplicate speech requests");
    (void)sequence.resume_speech();
    check(sequence.state().city.state3a == 0x11 && !sequence.speech_blocked(),
          "command-2 completion resumes at $874E and enters State 17");

    state.city.state3a = 0x1D;
    state.city.building_status_c8[3] = 0x7C;
    CatchSequence silent(payload, state, building_registers(), {0, 0, 0}, {0, 0, 0});
    const auto no_speech = silent.tick();
    check(!no_speech.blocking_request && silent.state().city.state3a == 0x11,
          "crossed-stream EA7A=0 path skips command 2 and exits immediately");
}

void test_state30_initialization_and_pointer4(const Payload& payload)
{
    auto active = catch_state(0x1E);
    active.city.sprites.pointers[4] = 0x0A;
    CatchSequence shifting(payload, active, building_registers(), {2, 0x20, 0}, {0, 0, 4});
    (void)shifting.tick({0, 4});
    check(shifting.state().city.sprites.pointers[4] == 0x0B &&
              shifting.state().city.state3a == 0x1E,
          "$9A0C replaces pointer-4 bit 0 with frame bit 2");
    (void)shifting.tick({0, 0});
    check(shifting.state().city.sprites.pointers[4] == 0x0A,
          "repeated $9A0C updates keep pointer 4 in the original $0A/$0B pair");

    auto threshold = catch_state(0x1E);
    threshold.city.sprites.x[7] = 0x76;
    threshold.city.sprites.y[7] = 0x88;
    threshold.city.sprites.pointers[0] = 0x25;
    CatchSequence initialized(payload, threshold, building_registers(), {1, 0x9C, 0}, {0, 5, 9});
    (void)initialized.tick();
    const auto& sprites = initialized.state().city.sprites;
    check(initialized.state().city.state3a == 0x1F &&
              initialized.state().city.key17 == 0 &&
              initialized.registers().deformation7b == 0x7F,
          "State 30 threshold initializes countdown and advances to State 31");
    check(sprites.pointers[0] != 0 && sprites.pointers[1] == 0 &&
              sprites.pointers[3] == 0 && sprites.target_x[0] == 0x76 &&
              sprites.target_y[4] == 0x88 && initialized.registers().catch_flag_ea88 == 0,
          "State 30 retains pointer 0 while targeting sprite 0 and ghost at sprite 7");
}

void test_state31_capture_and_blocking_completion(const Payload& payload)
{
    auto bouncing = catch_state(0x1F);
    bouncing.city.sprites.pointers[0] = 0;
    CatchSequence bounce(payload, bouncing, building_registers(), {0, 0, 0}, {0x0D, 0, 0});
    (void)bounce.tick();
    check(bounce.state().city.sprites.pointers[5] == 0x14 &&
              bounce.state().city.sprites.pointers[6] == 0x14 &&
              bounce.registers().scratch23 == 3,
          "$883A bounce adds doubled phase bits to the buster pointer base");

    auto state = catch_state(0x1F);
    auto& city = state.city;
    city.owned_mask6d = 0x40;
    city.empty_traps6b = 3;
    city.current_building6e = 2;
    city.building_status_c8[2] = 0x0C;
    city.balance57 = {0, 0x95, 0};
    city.sprites.pointers[0] = 0x25;
    city.sprites.x[0] = city.sprites.target_x[0] = 0x70;
    city.sprites.y[0] = city.sprites.target_y[0] = 0x80;
    CatchSequence capture(payload, state, building_registers(), {5, 0, 0}, {2, 4, 0});
    const auto captured = capture.tick({0, 0});
    check(!captured.blocking_request && capture.state().city.sprites.pointers[7] == 0x38 &&
              capture.state().city.sprites.pointers[0] == 0 &&
              capture.state().city.sprites.pointers[4] == 0 &&
              capture.registers().catch_flag_ea88 == 1,
          "trap arrival installs the full-trap sprite and removes ghost deformation");
    check(capture.registers().full_traps6c == 5 &&
              capture.state().city.empty_traps6b == 3 &&
              capture.state().city.balance57.byte57 == 1 &&
              capture.state().city.balance57.byte58 == 0x05 &&
              capture.state().city.building_status_c8[2] == 0,
          "capture fills capacity and applies the status-indexed BCD award with carry");

    auto ending = catch_state(0x1F);
    ending.city.sprites.pointers[0] = 0;
    ending.city.sprites.x[7] = ending.city.sprites.target_x[7] = 0x66;
    ending.city.sprites.y[7] = ending.city.sprites.target_y[7] = 0x91;
    CatchSequence completion(payload, ending, building_registers(), {0, 0, 0}, {1, 0, 0});
    const auto speech = completion.tick();
    check(speech.blocking_request && speech.audio_events ==
              std::vector<CatchAudioEvent>{{CatchAudioAction::speech_start, 1}} &&
              completion.state().city.state3a == 0x1F &&
              completion.state().city.sprites.enabled_mask == 0xE0,
          "expired State-31 countdown shuts down sprites before blocking command 1");
    (void)completion.resume_speech();
    check(completion.state().city.state3a == 0x20 &&
              completion.state().city.key17 == 0 &&
              completion.state().city.sprites.target_x[5] == 0x66 &&
              completion.state().city.sprites.target_x[6] == 0x78 &&
              completion.state().city.sprites.target_y[5] == 0x91,
          "command-1 completion resumes at $8823 and prepares State 32 targets");
}

void test_state_validation(const Payload& payload)
{
    try {
        CatchSequence invalid(payload, catch_state(0x20), building_registers(),
                              {0, 0, 0}, {0, 0, 0});
        (void)invalid;
        check(false, "constructor rejects State 32 owned by BuildingReturn");
    } catch (const std::invalid_argument&) {
        check(true, "constructor rejects State 32 owned by BuildingReturn");
    }
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_storage_capacity(payload);
        test_state28_paths(payload);
        test_state29_failure_and_speech(payload);
        test_state30_initialization_and_pointer4(payload);
        test_state31_capture_and_blocking_completion(payload);
        test_state_validation(payload);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
