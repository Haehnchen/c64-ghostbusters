#include "platform/sdl/launch_options.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace ghostbusters::platform::sdl {

LaunchOptions parse_launch_options(int argc, char* const argv[])
{
    LaunchOptions options;
    if (argc == 1) return options;
    const std::string_view mode = argv[1];
    if (argc == 2 && mode == "--smoke-test") {
        options.smoke = true;
    } else if (argc == 3 && mode == "--trace-input") {
        options.input_trace_path = argv[2];
    } else if (argc == 4 && mode == "--replay-input") {
        options.input_replay_path = argv[2];
        options.input_trace_path = argv[3];
    } else if ((argc == 4 || argc == 6) && mode == "--play-from") {
        options.input_replay_path = argv[2];
        options.play_from_name = argv[3];
        const auto& scene = options.play_from_name;
        if (scene == "title") options.play_from_state = 255;
        else if (scene == "dialog") options.play_from_state = 1;
        else if (scene == "marshmallow") options.play_from_state = 36;
        else if (scene == "zuul") options.play_from_state = 40;
        else if (scene == "victory") options.play_from_state = 42;
        else if (scene == "defeat") options.play_from_state = 46;
        else throw std::runtime_error("Unknown play-from scene");
        if (argc == 6) {
            if (std::string_view(argv[4]) != "--trace-input")
                throw std::runtime_error("Expected --trace-input after play-from scene");
            options.input_trace_path = argv[5];
        }
    } else {
        // Only retained regression/export entry points belong to the native CLI.
        // The removed dialog/shop/scroller exporters had no remaining consumers.
        constexpr std::array modes{
            "--dump-title", "--dump-city", "--dump-drive-controls",
            "--dump-building-entry", "--dump-building-controls",
            "--dump-building-return", "--dump-pink-visit", "--dump-beams",
            "--dump-catch", "--dump-catch-failure", "--dump-headquarters",
            "--dump-marshmallow", "--dump-marshmallow-bait", "--dump-zuul",
            "--dump-zuul-success", "--dump-ending-success",
            "--dump-ending-failure", "--dump-ending-poor"};
        if (argc != 3 || std::find(modes.begin(), modes.end(), mode) == modes.end()) {
            throw std::runtime_error(
                "Usage: ghostbusters [--smoke-test | --trace-input FILE | "
                "--replay-input FILE TRACE | --play-from FILE SCENE [--trace-input FILE] | "
                "--dump-SCENE DIRECTORY]");
        }
        options.diagnostic_mode = mode;
        options.diagnostic_directory = argv[2];
    }
    return options;
}

} // namespace ghostbusters::platform::sdl
