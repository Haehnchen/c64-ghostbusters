#include "audio/speech_assets.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using ghostbusters::audio::DecodedSpeech;
using ghostbusters::audio::load_speech_asset;
using ghostbusters::audio::speech_asset_info;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::uint64_t event_hash(const DecodedSpeech& speech)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& event : speech.events) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<std::uint8_t>(event.timer_tick >> shift);
            hash *= 1099511628211ULL;
        }
        hash ^= event.value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 1) {
        std::cerr << "usage: speech_assets_test\n";
        return 2;
    }
    (void)argv;
    try {
        constexpr std::uint32_t expected_ticks[] = {9065, 15143, 12701, 25897, 21474};
        constexpr std::size_t expected_events[] = {7100, 12338, 9724, 17768, 14296};
        constexpr std::uint64_t expected_hashes[] = {
            0xBC6C9BA69EEEAB6EULL, 0xBF2E5333996DF10AULL,
            0x66E2DF10261CCD18ULL, 0x34860D34453E19A5ULL,
            0xD4ED98F4621F230DULL};
        for (std::uint8_t command = 0; command < 5; ++command) {
            const auto embedded = load_speech_asset(command);
            const auto info = speech_asset_info(command);
            require(info.timer_latch == 0x65 && info.total_timer_ticks == expected_ticks[command] &&
                        info.event_count == expected_events[command] && info.encoded_bytes > 0 &&
                        embedded.timer_latch == info.timer_latch &&
                        info.total_timer_ticks == embedded.total_timer_ticks &&
                        info.event_count == embedded.events.size(),
                    "embedded speech metadata differs from its stream");
            require(event_hash(embedded) == expected_hashes[command],
                    "embedded speech event hash changed");
            if (command == 3) {
                require(embedded.events.back() ==
                            ghostbusters::audio::SpeechEvent{25896, 7},
                        "embedded command 3 lost its final timer-gap event");
            }
        }

        bool rejected = false;
        try {
            static_cast<void>(load_speech_asset(5));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "speech asset accepted a command outside 0..4");

        std::cout << "embedded speech assets are internally consistent\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
