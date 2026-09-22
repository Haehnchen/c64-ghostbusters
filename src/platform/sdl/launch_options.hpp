#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace ghostbusters::platform::sdl {

struct LaunchOptions {
    bool smoke = false;
    std::filesystem::path input_trace_path;
    std::filesystem::path input_replay_path;
    std::optional<std::uint8_t> play_from_state;
    std::string play_from_name;
    std::string diagnostic_mode;
    std::filesystem::path diagnostic_directory;
};

[[nodiscard]] LaunchOptions parse_launch_options(int argc, char* const argv[]);

} // namespace ghostbusters::platform::sdl
