#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "platform/sdl/application.hpp"
#include "platform/sdl/launch_options.hpp"
#include <cstdio>
#include <exception>

int main(int argc, char* argv[])
{
    using namespace ghostbusters::platform::sdl;
    int result = 1;
    ReplaySession replay;
    try {
        const auto options = parse_launch_options(argc, argv);
        do {
            // Destroy scene and audio owners before shutting down SDL.
            {
                Application application(options, replay);
                result = application.run();
            }
            if (result == 2) ++replay.generation;
            SDL_Quit();
        } while (result == 2);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        result = 1;
    }
    SDL_Quit();
    return result;
}
