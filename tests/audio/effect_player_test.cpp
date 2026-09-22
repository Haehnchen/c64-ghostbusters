#include "assets/effect_data.hpp"
#include "assets/payload.hpp"
#include "audio/effect_player.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using ghostbusters::assets::Payload;
using ghostbusters::assets::EffectData;
using ghostbusters::audio::EffectPlayer;
using ghostbusters::audio::SidWrite;

int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Function>
void expect_out_of_range(Function&& function, const char* message)
{
    try {
        function();
        check(false, message);
    } catch (const std::out_of_range&) {
    } catch (...) {
        check(false, message);
    }
}

void testNativeStreams(const Payload& payload)
{
    const EffectData data(payload);
    constexpr std::array<std::size_t, 5> starts{0x62, 0x6C, 0x7A, 0x85, 0x91};
    for (std::uint8_t index = 0; index < starts.size(); ++index) {
        check(data.stream(index) == starts[index],
              "effect starts retain their stable logical stream cursors");
    }
    expect_out_of_range([&] { (void)data.stream(5); },
                        "effect stream index five is rejected");
    check(data.relative_stream(0x9D, 0xFB) == 0x98,
          "playable relative branch stays within the native stream section");
    expect_out_of_range([&] { (void)data.relative_stream(0xFF, 0); },
                        "relative stream jump rejects a cursor beyond the section");
    expect_out_of_range([&] { (void)data.relative_stream(0x2A5, 0); },
                        "relative stream jump rejects an invalid cursor");

    const auto stream = payload.asset("audio/effects/streams");
    std::uint32_t hash = 2166136261U;
    for (const auto byte : stream) {
        hash ^= byte;
        hash *= 16777619U;
    }
    check(stream.size() == 60 && hash == 0x95186E9AU,
          "effect-only command content matches the pinned hash");

}

void testEffectTwo(const Payload& payload)
{
    EffectPlayer effect(payload);
    check(effect.start(2) == std::vector<SidWrite>{{0x13, 0}, {0x14, 0},
                                                     {0x11, 0}, {0x10, 0},
                                                     {0x12, 0}},
          "$3D24 clears voice 3 in its original store order");
    check(effect.active() && effect.voice3_suppressed(),
          "effect start marks voice 3 as active and suppresses music voice 3");

    check(effect.tick() == std::vector<SidWrite>{{0x13, 0x40}, {0x14, 0x44},
                                                  {0x11, 0x01}, {0x10, 0x11},
                                                  {0x12, 0x41}, {0x12, 0x41},
                                                  {0x0E, 0x18}, {0x0F, 0x0E},
                                                  {0x0F, 0x0D}},
          "effect 2's first IRQ emits the reference SID writes");
    check(effect.stateSnapshot().timer == 0x40,
          "effect 2 arms its 64-tick delay");

    for (unsigned tick = 0; tick < 63; ++tick) {
        const auto expected_pitch = static_cast<std::uint8_t>(
            (tick % 2U == 0U) ? 0x0F : 0x0D);
        check(effect.tick() == std::vector<SidWrite>{{0x0F, expected_pitch}},
              "effect 2's sustained ticks write its reference pitch modulation");
    }
    check(effect.active(), "effect 2 remains active before its terminating tick");
    check(effect.tick() == std::vector<SidWrite>{{0x12, 0x40}, {0x0F, 0x0D}},
          "effect 2 applies its terminal pitch delta and releases voice 3");
    check(!effect.active() && !effect.voice3_suppressed(),
          "effect 2 termination restores music voice 3");
    check(effect.tick().empty(), "inactive effects do not emit later writes");
    check(effect.stop() == std::vector<SidWrite>{{0x12, 0}},
          "$3BDC remains an unconditional voice-3 stop");
}

void testAllEffects(const Payload& payload)
{
    // The first call is tick zero.  These are the natural terminator ticks
    // for all five streams in the pinned asset.
    constexpr std::array<unsigned, 5> kEndTicks{36, 16, 64, 64, 120};
    const std::vector<SidWrite> expected_start{
        {0x13, 0}, {0x14, 0}, {0x11, 0}, {0x10, 0}, {0x12, 0}};

    for (std::uint8_t index = 0; index < kEndTicks.size(); ++index) {
        EffectPlayer effect(payload);
        check(effect.start(index) == expected_start,
              "all five effects clear voice 3 in the reference store order");
        const auto started = effect.stateSnapshot();
        check(started.active && started.voice3_suppressed,
              "all five effects expose an active snapshot");

        check(started.stream_cursor_valid && started.stream_cursor ==
                  std::array<std::uint16_t, 5>{0x62, 0x6C, 0x7A, 0x85, 0x91}[index],
              "active snapshots expose native stream cursors");
        EffectPlayer restored(payload);
        restored.restoreState(started);
        check(restored.stateSnapshot() == started,
              "effect snapshot round-trips through the offset adapter");
        auto invalid = started;
        invalid.stream_cursor = 0x009E;
        bool rejected = false;
        try {
            restored.restoreState(invalid);
        } catch (const std::out_of_range&) {
            rejected = true;
        }
        check(rejected, "active snapshots outside readable stream bytes are rejected");
        invalid = started;
        invalid.stream_cursor_valid = false;
        expect_out_of_range([&] { restored.restoreState(invalid); },
                            "active snapshots require a native stream cursor");
        restored.restoreState(started);

        unsigned ticks = 0;
        while (effect.active() && ticks <= 305) {
            check(effect.tick() == restored.tick(),
                  "restored effect emits the same ordered SID writes");
            ++ticks;
        }
        check(!effect.active() && !effect.voice3_suppressed(),
              "all five effects release voice 3 at termination");
        check(ticks == kEndTicks[index] + 1,
              "all five effects terminate at their reference tick");
        check(effect.tick().empty(), "terminated effects remain silent");
    }
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        testNativeStreams(payload);
        testEffectTwo(payload);
        testAllEffects(payload);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "effect player tests passed\n";
    return 0;
}
