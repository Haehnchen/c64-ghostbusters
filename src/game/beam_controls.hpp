#pragma once

#include "assets/capture_data.hpp"
#include "assets/marshmallow_data.hpp"
#include "game/building_controls.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ghostbusters::game {

// Persistent bytes used by State $1B outside DriveControlsState and
// BuildingControlsRegisters. There are deliberately no field defaults.
struct BeamControlsRegisters {
    BeamControlsRegisters(std::uint8_t phase79_value,
                          std::uint8_t scratch7a_value,
                          std::uint8_t post_failure_ea7a_value)
        : beam_phase79(phase79_value), beam_scratch7a(scratch7a_value),
          post_failure_ea7a(post_failure_ea7a_value)
    {
    }

    std::uint8_t beam_phase79;
    std::uint8_t beam_scratch7a;
    std::uint8_t post_failure_ea7a;
};

struct BeamControlsInput {
    std::uint8_t random06 = 0;
    std::uint8_t frame09 = 0;
    std::uint8_t joystick33 = 0xFF;
};

enum class BeamAudioAction { voice3_stop, voice3_start };

struct BeamAudioEvent {
    BeamAudioAction action;
    // Defined for both actions so events remain simple value objects. It is
    // zero for stop and the original effect index for start.
    std::uint8_t effect = 0;

    [[nodiscard]] bool operator==(const BeamAudioEvent&) const = default;
};

struct BeamControlsTickResult {
    std::vector<BeamAudioEvent> audio_events{};
    // Remaining special IRQs. A value of zero means normal common-frame
    // scheduling may resume.
    std::uint8_t wait_irqs = 0;
};

// Native boundary for State $1B at $859B-$869F. Crossed streams suspend the
// normal foreground handler; resume_irq() represents one observed raster IRQ.
class BeamControls {
public:
    BeamControls(const assets::Payload& payload, DriveControlsState state,
                 BuildingControlsRegisters building_registers,
                 BeamControlsRegisters registers);

    [[nodiscard]] BeamControlsTickResult tick(BeamControlsInput input = {});
    [[nodiscard]] BeamControlsTickResult resume_irq();

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
    [[nodiscard]] BeamControlsRegisters& registers() noexcept { return registers_; }
    [[nodiscard]] const BeamControlsRegisters& registers() const noexcept
    {
        return registers_;
    }
    [[nodiscard]] std::uint8_t pending_irqs() const noexcept { return pending_irqs_; }

private:
    void set_pointer(std::size_t sprite, std::uint8_t pointer);
    void update_ghost(BeamControlsInput input);
    void compute_beam_origins();
    void compute_beam_directions();
    void confine_ghost();
    void move_all_sprites();
    void move_control_target(std::size_t sprite, std::uint8_t joystick);
    void clamp_control_target(std::size_t sprite);
    void update_beam_sprites();
    [[nodiscard]] bool beams_crossed(bool reversed_targets) const;
    [[nodiscard]] bool fire_press(std::uint8_t joystick);
    void queue_notice(std::uint8_t index);
    void finish_failure(BeamControlsTickResult& result);
    void finish_normally(BeamControlsTickResult& result);

    assets::CaptureData capture_data_;
    assets::MarshmallowData notice_data_;
    DriveControlsState state_;
    BuildingControlsRegisters building_registers_;
    BeamControlsRegisters registers_;
    std::vector<std::uint8_t> scene_data_;
    std::array<std::uint8_t, 2> beam_origins_{}; // temporary $23/$24
    std::uint8_t pending_irqs_ = 0;
};

} // namespace ghostbusters::game
