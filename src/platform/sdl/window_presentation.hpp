#pragma once

#include <SDL3/SDL.h>

namespace ghostbusters::platform::sdl {

inline constexpr char kWindowTitle[] = "Ghostbusters - F11: Fullscreen / Window";

inline bool configure_presentation(SDL_Renderer* renderer, bool native_pixels = false)
{
    // The 320x200 source uses non-square pixels on a 4:3 display.
    return SDL_SetRenderLogicalPresentation(renderer, 320, native_pixels ? 200 : 240,
                                            SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

inline bool set_borderless_fullscreen(SDL_Window* window, bool enabled)
{
    // A null display mode selects the desktop, not an exclusive video mode.
    return SDL_SetWindowFullscreenMode(window, nullptr) &&
           SDL_SetWindowFullscreen(window, enabled);
}

} // namespace ghostbusters::platform::sdl
