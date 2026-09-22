#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ghostbusters::platform::sdl {

// A schedule contains only held host keys and durations. It cannot set game
// state, clocks, random values, inventory, buildings, or audio state.
class InputReplay {
public:
    static constexpr SDL_KeyboardID keyboard_id = static_cast<SDL_KeyboardID>(-1);
    explicit InputReplay(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("Cannot open input replay");
        std::string line;
        std::uint64_t total = 0;
        while (std::getline(file, line)) {
            line.resize(line.find('#') == std::string::npos ? line.size() : line.find('#'));
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            std::istringstream input(line);
            Segment segment;
            if (!(input >> segment.frames) || segment.frames == 0 ||
                segment.frames > 1'000'000 || (total += segment.frames) > 1'000'000)
                throw std::runtime_error("Invalid replay duration");
            std::string name;
            while (input >> name) {
                const auto key = SDL_GetKeyFromName(name.c_str());
                if (key == SDLK_UNKNOWN || key == SDLK_ESCAPE)
                    throw std::runtime_error("Invalid replay key: " + name);
                if (std::find(segment.keys.begin(), segment.keys.end(), key) != segment.keys.end())
                    throw std::runtime_error("Duplicate replay key");
                segment.keys.push_back(key);
            }
            segments_.push_back(std::move(segment));
        }
        if (segments_.empty()) throw std::runtime_error("Empty input replay");
    }

    [[nodiscard]] const std::vector<SDL_Keycode>& keys() const { return segments_.at(index_).keys; }
    [[nodiscard]] bool finished() const { return index_ == segments_.size(); }

    void queue_edges() const
    {
        for (const auto key : keys()) {
            if (std::find(previous_.begin(), previous_.end(), key) != previous_.end()) continue;
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = key;
            event.key.which = keyboard_id;
            event.key.down = true;
            if (!SDL_PushEvent(&event)) throw std::runtime_error(SDL_GetError());
        }
        for (const auto key : previous_) {
            if (std::find(keys().begin(), keys().end(), key) != keys().end()) continue;
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_UP;
            event.key.key = key;
            event.key.which = keyboard_id;
            if (!SDL_PushEvent(&event)) throw std::runtime_error(SDL_GetError());
        }
    }

    void advance()
    {
        previous_ = keys();
        if (++elapsed_ == segments_[index_].frames) { ++index_; elapsed_ = 0; }
    }

private:
    struct Segment { std::uint64_t frames = 0; std::vector<SDL_Keycode> keys; };
    std::vector<Segment> segments_;
    std::vector<SDL_Keycode> previous_;
    std::size_t index_ = 0;
    std::uint64_t elapsed_ = 0;
};

} // namespace ghostbusters::platform::sdl
