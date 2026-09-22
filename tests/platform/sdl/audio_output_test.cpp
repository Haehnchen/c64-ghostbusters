#include "platform/sdl/audio_output.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>

int main()
{
    try {
        if (!SDL_Init(SDL_INIT_AUDIO)) throw std::runtime_error(SDL_GetError());
        {
            ghostbusters::platform::sdl::AudioOutput output;
            std::array<float, 4800> silence{};
            output.push(silence);
            if (output.queued_frames() > silence.size()) throw std::runtime_error("Invalid queued frame count");
            auto moved = std::move(output);
            moved.push(std::span<const float>{});
            moved.push(silence);
            if (moved.queued_frames() > 2 * silence.size()) throw std::runtime_error("Invalid queue after move");
            moved.clear();
            if (moved.queued_frames() != 0) throw std::runtime_error("Clear must discard queued source PCM");
        }
        SDL_Quit();
        std::cout << "SDL audio device/queue smoke passed (no game-audio fidelity claim)\n";
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
