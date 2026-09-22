#pragma once

#include "assets/payload.hpp"
#include "game/input_frame.hpp"

#include <array>
#include <cstdint>

namespace ghostbusters::game {

// The host presents the two CIA1 joystick inputs as complete active-low
// bytes.  0xFF is the electrically released (neutral) value.  A matrix key
// uses the original raw index: bits 0..2 select the CIA Port-A row and bits
// 3..5 select the CIA Port-B column.
struct KeyboardScannerInput {
    std::array<bool, 64> keys{};
    std::uint8_t port_a = 0xFF;
    std::uint8_t port_b = 0xFF;

    constexpr KeyboardScannerInput() = default;
    constexpr KeyboardScannerInput(std::array<bool, 64> matrix,
                                   std::uint8_t port_a_value = 0xFF,
                                   std::uint8_t port_b_value = 0xFF)
        : keys(matrix), port_a(port_a_value), port_b(port_b_value) {}

    // Convenience for callers that build a single-key stimulus without
    // having to know the std::array<bool, 64> representation.
    [[nodiscard]] static constexpr KeyboardScannerInput key(std::uint8_t raw,
                                                             std::uint8_t port_a_value = 0xFF,
                                                             std::uint8_t port_b_value = 0xFF)
    {
        KeyboardScannerInput input;
        if (raw < input.keys.size()) input.keys[raw] = true;
        input.port_a = port_a_value;
        input.port_b = port_b_value;
        return input;
    }
};

// Persistent bytes owned by the original keyboard/joystick routines.  The
// idle bytes are deliberately kept in InputFrameState: the joystick and
// keyboard routines and the common frame controller share those locations.
struct KeyboardScannerState {
    std::uint8_t key17 = 0;
    std::uint8_t latch18 = 0;
    std::uint8_t raw19 = 0xFF;
    std::uint8_t joystick33 = 0x1F;
    std::uint8_t joystick34 = 0x1F;
};

struct KeyboardScannerResult {
    bool joystick_active = false;
    bool keyboard_scanned = false;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return keyboard_scanned;
    }
};

// Return the active-low CIA1 Port-B value for the current electrical matrix.
// `low_rows` and `low_columns` use one bits for lines driven low; this is the
// complemented form of the values written to the CIA ports.  The helper is
// intentionally independent of scanner state so a timed CIA adapter can
// evaluate the same ghost-contact network at each actual read cycle.
[[nodiscard]] std::uint8_t read_keyboard_matrix_port_b(
    const KeyboardScannerInput& input,
    std::uint8_t low_rows,
    std::uint8_t low_columns) noexcept;

// Native translation of $96CA-$9755.  poll_input first executes the
// joystick poll and the $02 gate from $7061-$706E, then runs the complete
// keyboard guard, matrix scan, and $9733-$9755 edge path.
[[nodiscard]] KeyboardScannerResult poll_input(
    const assets::Payload& payload,
    InputFrameState& frame,
    KeyboardScannerState& state,
    const KeyboardScannerInput& input);

} // namespace ghostbusters::game
