#pragma once

#include "assets/payload.hpp"
#include "assets/ui_data.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>

namespace ghostbusters::platform::sdl {

// Host bindings produce held C64 contacts. The game scanner owns repeat
// suppression and its single pending $17 byte; this adapter queues no events.
// Release arrows/Space before B, Tab or Pause: joystick activity suppresses
// the scanner's keyboard pass. A held key does not enqueue repeat presses.
struct HeldInput {
    std::array<bool, 64> matrix{};
    std::uint8_t port_a = 0xFF;
    std::uint8_t port_b = 0xFF;
};

inline void map_held_key(const assets::Payload& payload, HeldInput& input,
                         SDL_Keycode code, bool joystick_controls)
{
    switch (code) {
    case SDLK_UP: if (joystick_controls) input.port_b &= ~0x01; return;
    case SDLK_DOWN: if (joystick_controls) input.port_b &= ~0x02; return;
    case SDLK_LEFT: if (joystick_controls) input.port_b &= ~0x04; return;
    case SDLK_RIGHT: if (joystick_controls) input.port_b &= ~0x08; return;
    case SDLK_SPACE:
        if (joystick_controls) { input.port_b &= ~0x10; return; }
        break;
    case SDLK_TAB:
        if (joystick_controls) input.matrix[0x27] = true;
        return;
    case SDLK_PAUSE: input.matrix[0x3F] = true; return;
    case SDLK_F1: input.matrix[0x20] = true; return;
    case SDLK_F3: input.matrix[0x28] = true; return;
    case SDLK_RETURN: case SDLK_KP_ENTER: input.matrix[8] = true; return;
    case SDLK_BACKSPACE: case SDLK_DELETE: input.matrix[0] = true; return;
    default: break;
    }
    if (code >= 'a' && code <= 'z') code = code - 'a' + 'A';
    if (code > 0x7F || code == 0x21 || code == 0x2F) return;
    const auto keyboard = assets::UiData(payload).keyboard();
    for (unsigned raw = 0; raw < 64; ++raw) {
        if (keyboard[raw] == code) {
            input.matrix[raw] = true;
            return;
        }
    }
}

inline HeldInput sample_held_input(const assets::Payload& payload, bool joystick_controls)
{
    HeldInput result;
    int count = 0;
    const bool* held = SDL_GetKeyboardState(&count);
    const auto modifiers = SDL_GetModState();
    for (int scancode = 0; scancode < count; ++scancode) {
        if (!held[scancode]) continue;
        map_held_key(payload, result,
            SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(scancode), modifiers, false),
            joystick_controls);
    }
    return result;
}

} // namespace ghostbusters::platform::sdl
