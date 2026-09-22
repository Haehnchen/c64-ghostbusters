#include "assets/capture_data.hpp"
#include "assets/payload.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>

namespace {

using ghostbusters::assets::CaptureData;
using Payload = ghostbusters::assets::Payload;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
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

void check_range(std::span<const std::uint8_t> actual,
                 std::span<const std::uint8_t> region,
                 const char* message)
{
    check(actual.size() == region.size() &&
              std::equal(actual.begin(), actual.end(), region.begin()), message);
}

void test_building_assets(const CaptureData& data, const Payload& payload)
{
    check_range(data.building_charset(false),
                payload.asset("buildings/normal/charset"),
                "normal building charset maps to its named graphics range");
    check_range(data.building_charset(true),
                payload.asset("buildings/zuul/charset"),
                "Zuul charset maps to its named graphics range");
    check_range(data.building_screen(false),
                payload.asset("buildings/normal/screen"),
                "normal building screen maps to its named graphics range");
    check_range(data.building_screen(true),
                payload.asset("buildings/zuul/screen"),
                "Zuul screen maps to its named graphics range");
    check(data.building_color(false, 0x347) ==
              payload.asset("buildings/normal/colors").back() &&
              data.building_color(true, 0x347) ==
              payload.asset("buildings/zuul/colors").back(),
          "both building color ranges include their final byte");

    for (std::uint8_t overlay = 1; overlay < 4; ++overlay) {
        const auto number = std::to_string(overlay);
        const auto low_tiles = payload.asset(
            "buildings/overlays/low/" + number + "/tiles");
        check(data.low_overlay_tile(overlay, 0) == low_tiles.front(),
              "low overlay tile maps to building export");
        check_range(data.low_overlay_charset(overlay), payload.asset(
                        "buildings/overlays/low/" + number + "/charset"),
                    "low overlay charset maps to building export");
        check_range(data.high_overlay_screen(overlay), payload.asset(
                        "buildings/overlays/high/" + number + "/screen"),
                    "high overlay screen maps to building export");
        check_range(data.high_overlay_charset(overlay), payload.asset(
                        "buildings/overlays/high/" + number + "/charset"),
                    "high overlay charset maps to graphics export");
        check_range(data.descriptor_charset_patch(overlay), payload.asset(
                        "buildings/descriptors/" + number + "/charset_patch"),
                    "descriptor patch maps to building export");
    }
}

void test_capture_tables(const CaptureData& data)
{
    constexpr std::array<std::array<std::uint8_t, 2>, 8> deltas{{
        {0x01, 0x00}, {0x01, 0xFF}, {0x00, 0xFF}, {0xFF, 0xFF},
        {0xFF, 0x00}, {0xFF, 0x01}, {0x00, 0x01}, {0x01, 0x01},
    }};
    for (std::uint8_t direction = 0; direction < 8; ++direction) {
        check(data.ghost_delta(direction) == deltas[direction],
              "ghost delta matches its pinned native value");
    }
    constexpr std::array<std::uint8_t, 8> pointers{
        0x19, 0x19, 0x19, 0x19, 0x0A, 0x0E, 0x0E, 0x37};
    constexpr std::array<std::array<std::uint8_t, 2>, 8> positions{{
        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0x50, 0x50},
        {0xA8, 0xC7}, {0xA8, 0xC7}, {0xB0, 0xBA},
    }};
    constexpr std::array<std::array<std::uint8_t, 2>, 8> targets{{
        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0x50, 0x50},
        {0x90, 0xC7}, {0xA8, 0xC7}, {0x98, 0xBA},
    }};
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        check(data.initial_sprite_pointer(sprite) == pointers[sprite],
              "sprite pointer matches its pinned native value");
        check(data.initial_sprite_position(sprite) == positions[sprite],
              "sprite position matches its pinned native value");
        check(data.initial_sprite_target(sprite) == targets[sprite],
              "sprite target matches its pinned native value");
    }
    check(data.second_buster_target() == std::array<std::uint8_t, 2>{0x90, 0xC7},
          "second buster target matches its pinned native value");
    check(data.sprite_color(0x3F) == 0x28,
          "sprite color matches its pinned native value");
    check(data.vehicle_color(3) == 0x04,
          "vehicle color matches its pinned native value");
    check(data.building_descriptor(19) == 0x92,
          "building descriptor matches its pinned native value");
}

void test_bounds(const CaptureData& data)
{
    expect_out_of_range([&] { (void)data.building_descriptor(20); },
                        "building descriptor rejects index 20");
    expect_out_of_range([&] { (void)data.building_color(false, 0x348); },
                        "building color rejects its exclusive end");
    expect_out_of_range([&] { (void)data.low_overlay_tile(0, 0); },
                        "low overlay rejects the absent variant");
    expect_out_of_range([&] { (void)data.low_overlay_tile(1, 0x18); },
                        "low overlay rejects its exclusive end");
    expect_out_of_range([&] { (void)data.high_overlay_screen(4); },
                        "high overlay rejects variant four");
    expect_out_of_range([&] { (void)data.vehicle_charset(4); },
                        "vehicle charset rejects index four");
    expect_out_of_range([&] { (void)data.initial_sprite_pointer(8); },
                        "sprite pointer rejects index eight");
    expect_out_of_range([&] { (void)data.ghost_delta(8); },
                        "ghost delta rejects direction eight");
    expect_out_of_range([&] { (void)data.sprite_color(68); },
                        "sprite colors reject pointers beyond native sprites");
}

} // namespace

int main()
{
    try {
        const auto payload = Payload::embedded();
        const CaptureData data(payload);
        test_building_assets(data, payload);
        test_capture_tables(data);
        test_bounds(data);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
