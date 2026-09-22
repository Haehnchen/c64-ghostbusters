#pragma once

#include "assets/capture_data.hpp"
#include "game/beam_controls.hpp"

#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// Bytes retained by States $1C-$1F outside the state objects inherited from
// the building and beam handlers. There are deliberately no field defaults.
struct CatchSequenceRegisters {
    CatchSequenceRegisters(std::uint8_t deformation_count,
                           std::uint8_t full_traps,
                           std::uint8_t catch_flag,
                           std::uint8_t scratch23_value = 0)
        : deformation7b(deformation_count), full_traps6c(full_traps),
          catch_flag_ea88(catch_flag), scratch23(scratch23_value)
    {
    }

    std::uint8_t deformation7b;
    std::uint8_t full_traps6c;
    std::uint8_t catch_flag_ea88;
    // Temporary helper byte $23 survives handler return and is included in
    // byte-exact oracle snapshots even though no later catch state consumes it.
    std::uint8_t scratch23;
};

struct CatchSequenceInput {
    std::uint8_t random06 = 0;
    std::uint8_t frame09 = 0;
};

enum class CatchAudioAction { speech_start };

struct CatchAudioEvent {
    CatchAudioAction action;
    std::uint8_t command;

    [[nodiscard]] bool operator==(const CatchAudioEvent&) const = default;
};

struct CatchSequenceTickResult {
    std::vector<CatchAudioEvent> audio_events{};
    bool blocking_request = false;
};

// Native boundary for States $1C-$1F at $86A0-$8874. Blocking calls through
// $8DB9 are represented by a speech event; resume_speech() executes the code
// following that original JSR only after the runtime reports completion.
class CatchSequence {
public:
    CatchSequence(const assets::Payload& payload, DriveControlsState state,
                  BuildingControlsRegisters building_registers,
                  BeamControlsRegisters beam_registers,
                  CatchSequenceRegisters registers);

    [[nodiscard]] CatchSequenceTickResult tick(CatchSequenceInput input = {});
    [[nodiscard]] CatchSequenceTickResult resume_speech();

    [[nodiscard]] DriveControlsState& state() noexcept { return state_; }
    [[nodiscard]] const DriveControlsState& state() const noexcept { return state_; }
    [[nodiscard]] BuildingControlsRegisters& building_registers() noexcept
    {
        return building_registers_;
    }
    [[nodiscard]] const BuildingControlsRegisters& building_registers() const noexcept
    {
        return building_registers_;
    }
    [[nodiscard]] BeamControlsRegisters& beam_registers() noexcept
    {
        return beam_registers_;
    }
    [[nodiscard]] const BeamControlsRegisters& beam_registers() const noexcept
    {
        return beam_registers_;
    }
    [[nodiscard]] CatchSequenceRegisters& registers() noexcept { return registers_; }
    [[nodiscard]] const CatchSequenceRegisters& registers() const noexcept
    {
        return registers_;
    }
    [[nodiscard]] bool speech_blocked() const noexcept { return blocked_speech_ != 0; }

private:
    void set_pointer(std::size_t sprite, std::uint8_t pointer);
    void move_all_sprites();
    void update_ghost(CatchSequenceInput input);
    void update_deformation();
    void update_pointer4(std::uint8_t frame);
    void add_pk_energy(std::uint8_t amount, std::uint8_t random);
    void add_capture_award(std::uint8_t amount);
    void advance_state();
    [[nodiscard]] CatchSequenceTickResult request_speech(std::uint8_t command);
    void finish_return_targets();
    void tick_state28(CatchSequenceInput input);
    [[nodiscard]] CatchSequenceTickResult tick_state29(CatchSequenceInput input);
    void tick_state30(CatchSequenceInput input);
    [[nodiscard]] CatchSequenceTickResult tick_state31(CatchSequenceInput input);

    assets::CaptureData capture_data_;
    DriveControlsState state_;
    BuildingControlsRegisters building_registers_;
    BeamControlsRegisters beam_registers_;
    CatchSequenceRegisters registers_;
    std::vector<std::uint8_t> scene_data_;
    std::uint8_t blocked_speech_ = 0;
};

} // namespace ghostbusters::game
