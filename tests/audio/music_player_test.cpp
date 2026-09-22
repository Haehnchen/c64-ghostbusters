#include "assets/payload.hpp"
#include "audio/music_player.hpp"

#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace {

using ghostbusters::audio::MusicPlayer;
using ghostbusters::audio::SidWrite;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::uint32_t hashBytes(std::uint32_t hash, std::span<const std::uint8_t> bytes)
{
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 16777619U;
    }
    return hash;
}

void testNativeMusicSections(const ghostbusters::assets::Payload& payload)
{
    const ghostbusters::assets::MusicData data(payload);
    auto rejects = [&](auto action) {
        try { action(); return false; }
        catch (const std::out_of_range&) { return true; }
    };

    std::uint32_t sequence_hash = 2166136261U;
    for (std::uint8_t channel = 0; channel < 3; ++channel) {
        const auto sequence = data.sequence(channel);
        check(sequence.size() == 110 && sequence.back() == 0xFF,
              "native channel sequences retain their terminators");
        sequence_hash = hashBytes(sequence_hash, sequence);
    }
    check(sequence_hash == 0x5F3EF45CU,
          "native channel sequence content matches the pinned hash");

    std::uint32_t phrase_hash = 2166136261U;
    for (std::uint8_t phrase = 0; phrase < 74; ++phrase) {
        if (phrase == 38 || phrase == 39 || phrase == 58 || phrase == 59 ||
            phrase >= 71) {
            check(rejects([&] { (void)data.phrase(phrase); }),
                  "unused phrase slots remain invalid");
        } else {
            const auto bytes = data.phrase(phrase);
            check(!bytes.empty() && bytes.back() == 0xFF,
                  "native phrases retain their terminators");
            phrase_hash = hashBytes(phrase_hash, bytes);
        }
    }
    check(phrase_hash == 0x7086A944U,
          "native phrase content matches the pinned hash");
    check(rejects([&] { (void)data.sequence(3); }) &&
              rejects([&] { (void)data.voice_offset(3); }) &&
              rejects([&] { (void)data.phrase(74); }) &&
              rejects([&] { (void)data.phrase(255); }),
          "music channel and phrase indices reject exclusive bounds");
}

void testFirstSequence(const ghostbusters::assets::Payload& payload)
{
    MusicPlayer player(payload);
    const std::vector<SidWrite> expected{
        {0x0F, 0x0F}, {0x0E, 0xD2}, {0x12, 0x41}, {0x10, 0x40},
        {0x11, 0x00}, {0x13, 0x2F}, {0x14, 0xFF},
        {0x18, 0x06}, {0x08, 0x03}, {0x07, 0xF4}, {0x0B, 0x21},
        {0x09, 0x00}, {0x0A, 0x08}, {0x0C, 0x28}, {0x0D, 0x88},
        {0x01, 0x0E}, {0x00, 0x18}, {0x04, 0x41}, {0x02, 0x40},
        {0x03, 0x00}, {0x05, 0x2F}, {0x06, 0xFF},
    };
    check(player.tick(0, 0, 0) == expected,
          "first $93F0 call preserves voice order and SID write order");

    const auto state = player.stateSnapshot();
    check(state.sequence_index == std::array<std::uint8_t, 3>{0, 0, 0},
          "first event remains in the first sequence entry");
    check(state.phrase_index == std::array<std::uint8_t, 3>{2, 3, 2},
          "first phrases consume their exact byte counts");
    check(state.duration == std::array<std::uint8_t, 3>{3, 15, 15},
          "first phrase durations are decoded from low five bits");
    check(state.note == std::array<std::uint8_t, 3>{0x2D, 0x17, 0x2F},
          "first note indices match all three source phrases");
    check(state.command == std::array<std::uint8_t, 3>{0x23, 0xAF, 0x2F},
          "first command bytes retain their flags");
    check(state.instrument == std::array<std::uint8_t, 3>{0, 1, 0},
          "extended voice-two event selects instrument one");
    check(state.control == std::array<std::uint8_t, 3>{0x41, 0x21, 0x41},
          "instrument controls are cached for gate-off events");
    check(state.volume == 6, "extended event stores and writes volume six");
}

void testDisabledAndReset(const ghostbusters::assets::Payload& payload)
{
    MusicPlayer player(payload);
    const auto initial = player.stateSnapshot();
    check(player.tick(0, 0, 0x80).empty(),
          "negative $47 disables every SID write");
    check(player.stateSnapshot() == initial,
          "disabled tick leaves all player state untouched");

    static_cast<void>(player.tick(0, 0, 0));
    const auto active = player.stateSnapshot();
    player.reset();
    const auto reset = player.stateSnapshot();
    check(reset.sequence_index == std::array<std::uint8_t, 3>{},
          "reset clears sequence indices");
    check(reset.phrase_index == std::array<std::uint8_t, 3>{},
          "reset clears phrase indices");
    check(reset.duration == std::array<std::uint8_t, 3>{},
          "reset clears duration counters");
    check(reset.note == active.note && reset.command == active.command &&
              reset.control == active.control && reset.instrument == active.instrument &&
              reset.volume == active.volume,
          "$93E2 reset preserves the other music state bytes");
}

void testTitleSequencesStop(const ghostbusters::assets::Payload& payload)
{
    MusicPlayer player(payload);
    for (unsigned call = 0; call < 3489; ++call) {
        static_cast<void>(player.tick(0, 0, 0));
    }
    const auto state = player.stateSnapshot();
    check(state.duration == std::array<std::uint8_t, 3>{0xFF, 0xFF, 0xFF},
          "all three pinned title sequences eventually reach $FF");
    check(player.tick(0, 0, 0).empty(),
          "stopped title channels stay silent on later sequence phases");
}

void testLoadedEventSkipsSameTickModulation(const ghostbusters::assets::Payload& payload)
{
    MusicPlayer player(payload);
    ghostbusters::audio::MusicPlayerSnapshot before;
    before.sequence_index = {12, 12, 12};
    before.phrase_index = {15, 14, 29};
    before.duration = {0, 0, 0};
    before.note = {59, 28, 95};
    before.command = {3, 3, 1};
    before.control = {33, 33, 129};
    before.instrument = {5, 1, 3};
    before.volume = 7;
    player.restoreState(before);

    const std::vector<SidWrite> expected{
        {0x0F, 0xFD}, {0x0E, 0x2E}, {0x12, 0x81}, {0x10, 0x00},
        {0x11, 0x08}, {0x13, 0x14}, {0x14, 0x00},
        {0x08, 0x05}, {0x07, 0xED}, {0x0B, 0x21}, {0x09, 0x00},
        {0x0A, 0x08}, {0x0C, 0x28}, {0x0D, 0x88}, {0x18, 0x07},
        {0x01, 0x0F}, {0x00, 0xD2}, {0x04, 0x21}, {0x02, 0x00},
        {0x03, 0x08}, {0x05, 0x88}, {0x06, 0x88},
    };
    check(player.tick(2, 0xEC, 0) == expected,
          "loaded frame-1442 events skip same-tick modulation frequency writes");

    const auto after = player.stateSnapshot();
    check(after.sequence_index == std::array<std::uint8_t, 3>{12, 13, 12} &&
              after.phrase_index == std::array<std::uint8_t, 3>{18, 0, 31} &&
              after.duration == std::array<std::uint8_t, 3>{1, 3, 1} &&
              after.note == std::array<std::uint8_t, 3>{47, 30, 95} &&
              after.command == std::array<std::uint8_t, 3>{129, 3, 1} &&
              after.control == std::array<std::uint8_t, 3>{33, 33, 129} &&
              after.instrument == std::array<std::uint8_t, 3>{8, 1, 3} &&
              after.volume == 7,
          "frame-1442 music state still follows all three original load events");
}

} // namespace

int main()
{
    try {
        const auto payload =
            ghostbusters::assets::Payload::embedded();
        testFirstSequence(payload);
        testNativeMusicSections(payload);
        testDisabledAndReset(payload);
        testTitleSequencesStop(payload);
        testLoadedEventSkipsSameTickModulation(payload);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "Music player tests passed\n";
    return 0;
}
