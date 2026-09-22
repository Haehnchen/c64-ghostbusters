#include "platform/sdl/launch_options.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using ghostbusters::platform::sdl::parse_launch_options;

namespace {
auto parse(std::initializer_list<const char*> args)
{
    std::vector<std::string> storage{"ghostbusters"};
    for (const auto* arg : args) storage.emplace_back(arg);
    std::vector<char*> argv;
    for (auto& arg : storage) argv.push_back(arg.data());
    return parse_launch_options(static_cast<int>(argv.size()), argv.data());
}

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void rejected(std::initializer_list<const char*> args)
{
    try { (void)parse(args); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid or retired option was accepted");
}
}

int main()
{
    try {
        require(!parse({}).smoke, "Default launch must be interactive");
        require(parse({"--smoke-test"}).smoke, "Smoke option lost");
        require(parse({"--trace-input", "input.jsonl"}).input_trace_path == "input.jsonl",
                "Trace path lost");
        const auto replay = parse({"--replay-input", "route.inputs", "trace.jsonl"});
        require(replay.input_replay_path == "route.inputs" &&
                    replay.input_trace_path == "trace.jsonl", "Replay paths lost");
        for (const auto& [name, state] : std::vector<std::pair<const char*, int>>{
                 {"title", 255}, {"dialog", 1}, {"marshmallow", 36},
                 {"zuul", 40}, {"victory", 42}, {"defeat", 46}}) {
            const auto options = parse({"--play-from", "route.inputs", name,
                                        "--trace-input", "trace.jsonl"});
            require(options.play_from_state == state && options.play_from_name == name &&
                        options.input_trace_path == "trace.jsonl", "Live handoff option lost");
        }
        for (const auto* mode : {"--dump-title", "--dump-city", "--dump-drive-controls",
             "--dump-building-entry", "--dump-building-controls", "--dump-building-return",
             "--dump-pink-visit", "--dump-beams", "--dump-catch", "--dump-catch-failure",
             "--dump-headquarters", "--dump-marshmallow", "--dump-marshmallow-bait",
             "--dump-zuul", "--dump-zuul-success", "--dump-ending-success",
             "--dump-ending-failure", "--dump-ending-poor"}) {
            const auto options = parse({mode, "output"});
            require(options.diagnostic_mode == mode && options.diagnostic_directory == "output",
                    "Retained scene diagnostic lost");
        }
        for (const auto* mode : {"--unknown", "--dump-dialog", "--dump-scroller",
                                 "--dump-vehicle", "--dump-equipment",
                                 "--dump-city-controls", "--dump-drive-entry"})
            rejected({mode, "output"});
        rejected({"--smoke-test", "extra"});
        rejected({"--replay-input", "route.inputs"});
        rejected({"--play-from", "route.inputs", "unknown"});
        rejected({"--play-from", "route.inputs", "zuul", "--unknown", "trace"});
        rejected({"--dump-zuul"});
        rejected({"--dump-zuul", "output", "extra"});
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
