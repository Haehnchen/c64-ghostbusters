#include "game/keyboard_scanner.hpp"
#include "assets/ui_data.hpp"

#include <algorithm>
#include <array>

namespace ghostbusters::game {
namespace {

constexpr std::array<std::uint8_t, 8> kRowSelectMasks{
    0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F,
};
constexpr std::array<std::uint8_t, 8> kColumnMasks{
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
};

// The keyboard is a switch matrix without per-switch diodes.  Once one row
// is driven low, a pressed key can therefore connect that row to a column and
// continue through another pressed key into a different row.  The connected
// component below models the resulting CIA read, including the usual three
// corner rectangle ghost contact.  It intentionally models contacts only;
// electrical timing and analog settling are outside the native input
// boundary.
} // namespace

std::uint8_t read_keyboard_matrix_port_b(
    const KeyboardScannerInput& input,
    const std::uint8_t low_rows,
    const std::uint8_t low_columns) noexcept
{
    std::array<bool, 8> rows{};
    std::array<bool, 8> columns{};
    std::array<unsigned, 16> pending{};
    unsigned head = 0;
    unsigned tail = 0;

    for (unsigned row = 0; row < 8; ++row) {
        if ((low_rows & kColumnMasks[row]) == 0) continue;
        rows[row] = true;
        pending[tail++] = row;
    }
    for (unsigned column = 0; column < 8; ++column) {
        if ((low_columns & kColumnMasks[column]) == 0) continue;
        columns[column] = true;
        pending[tail++] = 8 + column;
    }

    while (head != tail) {
        const unsigned node = pending[head++];
        if (node < 8) {
            const unsigned row = node;
            for (unsigned column = 0; column < 8; ++column) {
                const auto raw = static_cast<std::uint8_t>((column << 3) | row);
                if (!input.keys[raw] || columns[column]) continue;
                columns[column] = true;
                pending[tail++] = 8 + column;
            }
        } else {
            const unsigned column = node - 8;
            for (unsigned row = 0; row < 8; ++row) {
                const auto raw = static_cast<std::uint8_t>((column << 3) | row);
                if (!input.keys[raw] || rows[row]) continue;
                rows[row] = true;
                pending[tail++] = row;
            }
        }
    }

    std::uint8_t value = 0xFF;
    for (unsigned column = 0; column < 8; ++column) {
        if (columns[column]) value = static_cast<std::uint8_t>(
            value & static_cast<std::uint8_t>(~kColumnMasks[column]));
    }
    return value;
}

namespace {

[[nodiscard]] KeyboardScannerResult poll_with_table(
    const std::array<std::uint8_t, 64>& translation,
    InputFrameState& frame,
    KeyboardScannerState& state,
    const KeyboardScannerInput& input)
{
    KeyboardScannerResult result;

    // $96CA-$96E7: CIA1 Port A is driven while Port B is read.  Only the
    // lower five lines are joystick inputs in the original routine.
    // Port-A joystick lows are also electrical low sources while $DC00 is
    // written as $FF.  A held matrix contact can consequently pull a Port-B
    // column low and become visible to the original $96D6 read.  This is why
    // the effective byte is calculated before applying the five-bit mask.
    const auto joystick_port_b = read_keyboard_matrix_port_b(
        input, static_cast<std::uint8_t>(~input.port_a),
        static_cast<std::uint8_t>(~input.port_b));
    state.joystick33 = static_cast<std::uint8_t>(joystick_port_b & 0x1F);
    result.joystick_active = state.joystick33 != 0x1F;
    if (result.joystick_active) {
        frame.idle45 = 0;
        frame.idle46 = 0;
    }

    // $7064-$706E suppresses joystick values unless $02 is exactly $80.  It
    // runs after the joystick poll, so the idle reset above still happens on
    // a filtered input.  $34 is preserved while the gate is open, exactly as
    // the original zero-page store sequence does.
    if (frame.gate02 != 0x80) {
        state.joystick33 = 0x1F;
        state.joystick34 = 0x1F;
    }

    // $96E8-$96F5: an attached joystick on either CIA port prevents the
    // keyboard matrix writes from touching the key state.  The host values
    // are complete bytes, hence upper-bit activity also trips this guard even
    // though the joystick result above only retains five lines.
    if (input.port_a != 0xFF || joystick_port_b != 0xFF) return result;
    result.keyboard_scanned = true;

    // $96F7-$9731: start at raw index $3F and descend.  Every accepted
    // position overwrites $19, so the final accepted position is the smallest
    // matrix index, just as on the 6510.
    state.raw19 = 0xFF;
    for (int raw = 0x3F; raw >= 0; --raw) {
        const auto index = static_cast<std::uint8_t>(raw);
        const unsigned row = index & 0x07U;
        const unsigned column = index >> 3U;
        // Keep the actual CIA select/mask tables explicit at this boundary.
        // Their values are not needed after selecting a row, but referencing
        // them documents the same active-low operation as $3753/$374B.
        const auto selected_row = static_cast<std::uint8_t>(~kRowSelectMasks[row]);
        const auto column_mask = kColumnMasks[column];
        if ((read_keyboard_matrix_port_b(input, selected_row, 0) & column_mask) != 0) continue;
        if (translation[index] != 0x21) state.raw19 = index;
    }

    // $9733-$9755: translated edge/latch handling.  $17 is an event byte
    // consumed by the current scene; this scanner never clears it.
    if (state.raw19 == 0xFF) {
        state.latch18 = 0;
        return result;
    }

    const auto translated = translation[state.raw19];
    if (translated == state.latch18) return result;
    if (translated == 0x2F) {
        state.latch18 = 0;
        return result;
    }

    state.key17 = translated;
    state.latch18 = translated;
    frame.idle45 = 0;
    frame.idle46 = 0;
    return result;
}

} // namespace

KeyboardScannerResult poll_input(
    const assets::Payload& payload,
    InputFrameState& frame,
    KeyboardScannerState& state,
    const KeyboardScannerInput& input)
{
    std::array<std::uint8_t, 64> translation{};
    const auto bytes = assets::UiData(payload).keyboard();
    std::copy(bytes.begin(), bytes.end(), translation.begin());
    return poll_with_table(translation, frame, state, input);
}

} // namespace ghostbusters::game
