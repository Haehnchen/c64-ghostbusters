#include "assets/payload.hpp"
#include "assets/vehicle_data.hpp"
#include "game/vehicle_graphics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::assets::VehicleData;

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

constexpr std::uint8_t reverse_pairs(std::uint8_t value)
{
    return static_cast<std::uint8_t>(((value & 0x03U) << 6U) |
                                     ((value & 0x0CU) << 2U) |
                                     ((value & 0x30U) >> 2U) |
                                     ((value & 0xC0U) >> 6U));
}

template <typename Iterator>
std::uint32_t fnv1a(Iterator first, Iterator last)
{
    std::uint32_t hash = 2166136261U;
    while (first != last) {
        hash = (hash ^ *first++) * 16777619U;
    }
    return hash;
}

std::array<std::uint8_t, VehicleData::kCharsetBytes>
independent_charset(const Payload& payload, std::uint8_t vehicle)
{
    constexpr std::array<const char*, VehicleData::kVehicleCount> names{
        "vehicles/compact", "vehicles/hearse", "vehicles/wagon",
        "vehicles/performance"};
    const auto source = payload.asset(names[vehicle]);
    std::array<std::uint8_t, VehicleData::kCharsetBytes> expected{};
    std::copy(source.begin(), source.end(), expected.begin());
    return expected;
}

void test_all_vehicle_graphics(const Payload& payload, const VehicleData& data)
{
    constexpr std::array<std::uint32_t, 4> raw_hashes{
        0x1813E867U, 0x317897C7U, 0x2A051878U, 0xBA4B03F4U};
    constexpr std::array<std::uint32_t, 4> mirrored_hashes{
        0xF06E9DADU, 0x4C53A55FU, 0x7429DB34U, 0xC327E8C7U};
    constexpr std::array<std::uint8_t, 4> multicolor2{3, 6, 6, 4};

    for (std::uint8_t vehicle = 0; vehicle < VehicleData::kVehicleCount; ++vehicle) {
        const auto expected = independent_charset(payload, vehicle);
        check(data.vehicle_charset(vehicle) == expected,
              "adapter returns each ready native vehicle sheet");
        check(fnv1a(expected.begin(), expected.end()) == raw_hashes[vehicle],
              "each reconstructed vehicle has its pinned raw hash");

        ghostbusters::video::CharacterFrame frame;
        ghostbusters::game::prepare_vehicle_graphics(payload, frame, vehicle);
        bool raw_matches = true;
        bool mirrored_matches = true;
        for (std::size_t offset = 0; offset < expected.size(); ++offset) {
            raw_matches = raw_matches && frame.charset[0x0200 + offset] == expected[offset];
        }
        for (std::size_t block = 0; block < 6; ++block) {
            for (std::size_t offset = 0; offset < 128; ++offset) {
                const auto expected_byte = reverse_pairs(expected[(5 - block) * 128 + offset]);
                mirrored_matches = mirrored_matches &&
                    frame.charset[0x0500 + block * 128 + offset] == expected_byte;
            }
        }
        check(raw_matches && mirrored_matches,
              "all four rendered vehicle sheets preserve raw and mirrored glyphs");
        check(fnv1a(frame.charset.begin() + 0x0500, frame.charset.begin() + 0x0800) ==
                  mirrored_hashes[vehicle],
              "each rendered vehicle has its pinned mirrored hash");
        check(frame.multicolor1 == 1 && frame.multicolor2 == multicolor2[vehicle] &&
                  data.multicolor2(vehicle) == multicolor2[vehicle],
              "all four vehicle palettes match their pinned native table");
    }

    const auto compact = data.vehicle_charset(0);
    check(fnv1a(compact.begin(), compact.begin() + 256) == 0xE6A1D1C5U &&
              fnv1a(compact.begin() + 256, compact.begin() + 512) ==
                  0x76AD59BFU,
          "Compact preserves its cleared page 2 and pinned page-3 bytes");
}

void test_grid_and_drive_tables(const VehicleData& data)
{
    constexpr std::array<VehicleData::DriveSprite, 8> initial_sprites{{
        {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00},
        {0x00, 0x00, 0x04}, {0x00, 0x00, 0x05},
        {0x36, 0x00, 0x0C}, {0x6D, 0x00, 0x0C},
        {0x6D, 0x00, 0x0C}, {0x36, 0x00, 0x0C},
    }};
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        const auto initial = data.initial_drive_sprite(sprite);
        check(initial == initial_sprites[sprite],
              "all initial drive sprite records match their pinned values");
    }
    constexpr std::array<std::uint8_t, 4> vacuum_y{0x78, 0x70, 0x6C, 0x76};
    for (std::uint8_t vehicle = 0; vehicle < 4; ++vehicle) {
        check(data.vacuum_target_y(vehicle) == vacuum_y[vehicle],
              "all vacuum targets match their pinned native table");
    }
    const auto codes = data.grid_first_codes();
    constexpr std::array<std::uint8_t, 12> expected_codes{
        0x40, 0x50, 0x60, 0x70, 0x80, 0x90,
        0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0};
    bool codes_match = codes.size() == expected_codes.size();
    for (std::size_t column = 0; column < codes.size(); ++column) {
        codes_match = codes_match && codes[column] == expected_codes[column];
    }
    check(codes_match && codes.front() == 0x40 && codes.back() == 0xF0,
          "all 12 bounded vehicle-grid columns match their pinned values");

    constexpr std::array<std::uint8_t, 4> y{0x86, 0x87, 0x84, 0x94};
    constexpr std::array<std::uint8_t, 4> multicolor2{0x03, 0x06, 0x06, 0x04};
    constexpr std::array<std::uint8_t, 4> siren{0x06, 0x0E, 0x03, 0x01};
    constexpr std::array<std::uint8_t, 4> speed_limits{0x60, 0x70, 0x80, 0xA0};
    for (std::uint8_t index = 0; index < 4; ++index) {
        check(data.drive_sprite_y(index) == y[index] &&
                  data.multicolor2(index) == multicolor2[index],
              "all four vehicle Y positions and palettes have pinned values");
        check(data.siren_color(index) == siren[index] &&
                  data.siren_color(index) == siren[index],
              "all four siren phases have pinned colors");
        check(data.speed_limit(index) == speed_limits[index] &&
                  data.speed_limit(index) == speed_limits[index],
              "all four speed limits have pinned values");
    }
}

void test_invalid_indices(const Payload& payload, const VehicleData& data)
{
    expect_out_of_range([&] { (void)data.initial_drive_sprite(8); },
                        "initial drive sprites reject index eight");
    expect_out_of_range([&] { (void)data.vacuum_target_y(4); },
                        "vacuum target rejects vehicle four");
    expect_out_of_range([&] { (void)data.vehicle_charset(4); },
                        "vehicle charset rejects index four");
    expect_out_of_range([&] { (void)data.multicolor2(4); },
                        "vehicle palette rejects index four");
    expect_out_of_range([&] { (void)data.drive_sprite_y(4); },
                        "drive Y rejects index four");
    expect_out_of_range([&] { (void)data.speed_limit(4); },
                        "speed limit rejects index four");
    expect_out_of_range([&] { (void)data.siren_color(4); },
                        "siren color rejects phase four");
    expect_out_of_range([&] {
        ghostbusters::video::CharacterFrame frame;
        ghostbusters::game::prepare_vehicle_graphics(payload, frame, 4);
    }, "production vehicle renderer rejects index four");
}

} // namespace

int main()
{
    try {
        const auto payload = Payload::embedded();
        const VehicleData data(payload);
        test_all_vehicle_graphics(payload, data);
        test_grid_and_drive_tables(data);
        test_invalid_indices(payload, data);
        if (failures != 0) return 1;
        std::cout << "vehicle data tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
