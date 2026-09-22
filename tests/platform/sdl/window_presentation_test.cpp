#include "platform/sdl/window_presentation.hpp"
#include "platform/sdl/input.hpp"
#include "assets/embedded.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void checked(bool ok) { if (!ok) throw std::runtime_error(SDL_GetError()); }
}

int main()
{
    try {
        using namespace ghostbusters::platform::sdl;
        checked(SDL_Init(SDL_INIT_VIDEO));
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow(kWindowTitle, 1280, 720, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
        require(bool(window), "Window creation failed");
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
            SDL_CreateRenderer(window.get(), "software"), SDL_DestroyRenderer);
        require(bool(renderer), "Renderer creation failed");
        checked(configure_presentation(renderer.get()));
        SDL_FRect rect;
        checked(SDL_GetRenderLogicalPresentationRect(renderer.get(), &rect));
        require(rect.x == 160 && rect.y == 0 && rect.w == 960 && rect.h == 720,
                "Widescreen presentation must center a full-height 4:3 image");
        checked(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
        checked(SDL_RenderClear(renderer.get()));
        checked(SDL_SetRenderDrawColor(renderer.get(), 255, 255, 255, 255));
        const SDL_FRect game{0, 0, 320, 240};
        checked(SDL_RenderFillRect(renderer.get(), &game));
        // Read the whole output, including pixels outside the logical viewport.
        checked(SDL_SetRenderLogicalPresentation(renderer.get(), 0, 0,
                                                 SDL_LOGICAL_PRESENTATION_DISABLED));
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
            SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
        require(bool(pixels), "Cannot inspect rendered pixels");
        for (int x : {0, 159, 160, 1119, 1120, 1279}) {
            Uint8 r, g, b, a;
            checked(SDL_ReadSurfacePixel(pixels.get(), x, 360, &r, &g, &b, &a));
            const auto expected = x >= 160 && x < 1120 ? 255 : 0;
            require(r == expected && g == expected && b == expected,
                    "The 4:3 image must have black side bars without cropping");
        }
        checked(set_borderless_fullscreen(window.get(), true));
        checked(SDL_SyncWindow(window.get()));
        require((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0 &&
                    SDL_GetWindowFullscreenMode(window.get()) == nullptr,
                "Fullscreen must use the desktop, not an exclusive mode");
        checked(set_borderless_fullscreen(window.get(), false));
        checked(SDL_SyncWindow(window.get()));
        require((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) == 0,
                "Fullscreen must toggle back to a window");
        require(std::strcmp(SDL_GetWindowTitle(window.get()), kWindowTitle) == 0,
                "The window title must stay fixed");
        const auto payload = ghostbusters::assets::Payload::embedded();
        for (bool joystick : {false, true}) {
            HeldInput input;
            map_held_key(payload, input, SDLK_F11, joystick);
            require(input.port_a == 255 && input.port_b == 255 &&
                        std::none_of(input.matrix.begin(), input.matrix.end(), [](bool v) { return v; }),
                    "F11 must not reach a C64 key or joystick contact");
        }
        std::cout << "window presentation tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
    SDL_Quit();
    return 0;
}
