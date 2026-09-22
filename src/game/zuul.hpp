#pragma once

#include "assets/payload.hpp"
#include "game/building_controls.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ghostbusters::game {

// Bytes used by States $28-$2C outside DriveControlsState and the retained
// building controls. There are deliberately no field defaults: imported
// snapshots must make the overlapping zero-page and $EAxx bytes explicit.
struct ZuulRegisters {
    ZuulRegisters(std::uint8_t scratch23_value,
                  std::uint8_t scratch24_value,
                  std::uint8_t scratch25_value,
                  std::uint8_t gate_phase,
                  std::uint8_t gatekeeper_remaining,
                  std::uint8_t keymaster_remaining,
                  std::uint8_t beam_phase,
                  std::uint8_t beam_scratch)
        : scratch23(scratch23_value), scratch24(scratch24_value),
          scratch25(scratch25_value), gate_phase_ea77(gate_phase),
          gatekeeper_ea78(gatekeeper_remaining),
          keymaster_ea79(keymaster_remaining), beam_phase79(beam_phase),
          beam_scratch7a(beam_scratch)
    {
    }

    std::uint8_t scratch23;
    std::uint8_t scratch24;
    std::uint8_t scratch25;
    std::uint8_t gate_phase_ea77;
    std::uint8_t gatekeeper_ea78;
    std::uint8_t keymaster_ea79;
    std::uint8_t beam_phase79;
    std::uint8_t beam_scratch7a;
};

struct ZuulInput {
    std::uint8_t irq_counter08 = 0;
    std::uint8_t frame09 = 0;
    std::uint8_t joystick33 = 0xFF;
    std::uint8_t sprite_sprite_collision_d01e = 0;
};

enum class ZuulAudioAction { speech_start };

struct ZuulAudioEvent {
    ZuulAudioAction action;
    std::uint8_t command;

    [[nodiscard]] bool operator==(const ZuulAudioEvent&) const = default;
};

struct ZuulTickResult {
    std::vector<ZuulAudioEvent> audio_events{};
    bool blocking_request = false;
    // $8A9C consumes the collision latch only on the gated collision path.
    bool collision_read = false;
    // $8B77 consumes it unconditionally at the State-$29 animation tail.
    bool collision_clear_read = false;
};

enum class ZuulTransition : std::uint8_t {
    initialize = 0x28,
    rooftop = 0x29,
    gate = 0x2A,
    climb_setup = 0x2B,
    climb = 0x2C,
    insufficient_balance_ending = 0x2D,
    gate_collision_ending_handoff = 0x2E,
    reward_handoff = 0x36,
};

// Native boundary for States $28-$2C at $8A2C-$8C73. Blocking calls through
// $8DB9 suspend the foreground handler; resume_speech() executes only the
// continuation after that JSR and does not stand in for an IRQ/frame tick.
class Zuul {
public:
    Zuul(const assets::Payload& payload, DriveControlsState state,
         BuildingControlsRegisters building_registers,
         ZuulRegisters registers);

    [[nodiscard]] ZuulTickResult tick(ZuulInput input = {});
    [[nodiscard]] ZuulTickResult resume_speech();

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
    [[nodiscard]] ZuulRegisters& registers() noexcept { return registers_; }
    [[nodiscard]] const ZuulRegisters& registers() const noexcept { return registers_; }
    [[nodiscard]] std::uint8_t raw_state() const noexcept
    {
        return state_.city.state3a;
    }
    [[nodiscard]] ZuulTransition transition() const noexcept
    {
        return static_cast<ZuulTransition>(raw_state());
    }
    [[nodiscard]] bool speech_blocked() const noexcept { return blocked_speech_ != 0; }
    // Present only when this tick inserted a static climb row.
    [[nodiscard]] std::optional<std::size_t> climb_color_offset() const noexcept
    {
        return climb_color_offset_;
    }

private:
    void set_pointer(std::size_t sprite, std::uint8_t pointer);
    void reset_sprite_state();
    void advance_state();
    void reset_gate();
    void move_sprite(std::size_t sprite);
    void move_control_target(std::size_t sprite, std::uint8_t joystick);
    void animate_buster5(std::uint8_t frame);
    [[nodiscard]] bool sprite_at_target(std::size_t sprite) const noexcept;
    [[nodiscard]] ZuulTickResult update_controlled_sprite(ZuulInput input);
    [[nodiscard]] ZuulTickResult animate_gate(std::uint8_t irq_counter);
    void scroll_climb_background();
    void update_climb_sprites();
    void add_reward();

    [[nodiscard]] ZuulTickResult tick_state40();
    [[nodiscard]] ZuulTickResult tick_state41(ZuulInput input);
    [[nodiscard]] ZuulTickResult tick_state42();
    [[nodiscard]] ZuulTickResult tick_state43();
    [[nodiscard]] ZuulTickResult tick_state44();
    [[nodiscard]] ZuulTickResult request_speech(std::uint8_t command,
                                                ZuulInput input = {});

    const assets::Payload& payload_;
    DriveControlsState state_;
    BuildingControlsRegisters building_registers_;
    ZuulRegisters registers_;
    std::vector<std::uint8_t> scene_data_;
    std::uint8_t blocked_speech_ = 0;
    ZuulInput blocked_input_{};
    std::optional<std::size_t> climb_color_offset_;
};

} // namespace ghostbusters::game
