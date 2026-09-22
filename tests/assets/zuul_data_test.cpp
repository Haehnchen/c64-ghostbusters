#include "assets/payload.hpp"
#include "assets/zuul_data.hpp"
#include "game/title_screen.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::assets::ZuulData;

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

std::uint32_t fnv1a(std::span<const std::uint8_t> bytes)
{
    std::uint32_t value = 2166136261U;
    for (const auto byte : bytes) value = (value ^ byte) * 16777619U;
    return value;
}

void test_named_lookup_values(const ZuulData& data)
{
    std::vector<std::uint8_t> gate;
    for (std::uint8_t phase = 0; phase < 32; ++phase) {
        const auto frame = data.gate_frame(phase);
        gate.insert(gate.end(), {frame.y, frame.x, frame.packed_pointers});
    }
    check(fnv1a(gate) == 0x0328D6E7U, "all gate-frame fields retain their pinned hash");

    const auto first = data.gate_frame(0);
    const auto last = data.gate_frame(31);
    check(first.y == 0x6C && first.x == 0x40 &&
              first.packed_pointers == 0x54,
          "first gate frame has the pinned Y, X and packed pointers");
    check(last.y == 0x6C && last.x == 0x55 &&
              last.packed_pointers == 0x54,
          "last gate frame stays inside all three 32-byte tables");

    std::array<std::uint8_t, 4> pointers{};
    for (std::size_t index = 0; index < pointers.size(); ++index) {
        pointers[index] = data.rooftop_pointer(index);
    }
    check(pointers == std::array<std::uint8_t, 4>{0x2B, 0x2C, 0x2D, 0x2E},
          "rooftop sprite pointers are exposed as one named table");
    check(data.sprite_color(0x2B) == 0x01 &&
              data.sprite_color(0x3C) == 0x02,
          "normal and mirrored beam pointers resolve through named colors");
}

void test_climb_rows(const ZuulData& data)
{
    std::vector<std::uint8_t> screens, colors;
    for (std::uint8_t phase = 0; phase < 3; ++phase) {
        const auto row = data.generated_climb_row(phase);
        screens.insert(screens.end(), row.screen.begin(), row.screen.end());
        colors.insert(colors.end(), row.colors.begin(), row.colors.end());
    }
    check(fnv1a(screens) == 0x09E654F0U && fnv1a(colors) == 0x17458645U,
          "all generated cells retain their pinned screen/color hashes");

    const auto generated = data.generated_climb_row(2);
    check(generated.screen[39] == 0x4C && generated.colors[39] == 0x0E &&
              generated.screen[0] == 0x4C && generated.colors[0] == 0x0E,
          "phase-two generated row preserves reverse source indexing");

    const auto first = data.static_climb_row(0);
    check(first.screen[0] == 0x4C && first.screen[39] == 0x4C &&
              first.colors[0] == 0x0E,
          "first static climb row exposes decoded colors");
    const auto last = data.static_climb_row(20);
    check(last.screen[0] == 0x4C && last.screen[39] == 0x4C &&
              last.colors[0] == 0x0E,
          "last static climb row remains within the exported graphics asset");
    screens.clear();
    colors.clear();
    for (std::uint8_t index = 0; index < 21; ++index) {
        const auto row = data.static_climb_row(index);
        screens.insert(screens.end(), row.screen.begin(), row.screen.end());
        colors.insert(colors.end(), row.colors.begin(), row.colors.end());
    }
    check(fnv1a(screens) == 0x46C693A5U && fnv1a(colors) == 0xE458CD17U,
          "all static cells retain their pinned screen/color hashes");
}

void test_mirrored_sheets(const ZuulData& data)
{
    auto scene = std::vector<std::uint8_t>(data.scene_graphics().begin(),
                                           data.scene_graphics().end());
    scene.resize(0x1100);
    const auto source_73c = scene[0x073C];
    const auto source_73e = scene[0x073E];
    const auto source_83c = scene[0x083C];
    const auto source_83e = scene[0x083E];
    data.mirror_sprite_sheets(scene);

    check(scene[0x0FFC] == reverse_pairs(source_73e) &&
              scene[0x0FFE] == reverse_pairs(source_73c) &&
              scene[0x10FC] == reverse_pairs(source_83e) &&
              scene[0x10FE] == reverse_pairs(source_83c),
          "mirrored sheets reverse byte order and pixel pairs at both upper bounds");
    check(scene[0x0F03] == 0x3C && scene[0x0FFE] == 0x0C &&
              scene[0x1003] == 0x3C && scene[0x10F5] == 0xC0 &&
              scene[0x0F00] == 0 && scene[0x10FF] == 0,
          "mirrored sheets contain pinned bytes and retain sprite padding");
    check(scene.size() == 0x1100,
          "mirroring retains the exact pointer-$00-through-$43 scene size");
}

void test_semantic_bounds(const ZuulData& data)
{
    expect_out_of_range([&] { (void)data.gate_frame(32); },
                        "gate frame 32 is rejected");
    expect_out_of_range([&] { (void)data.rooftop_pointer(4); },
                        "fifth rooftop pointer is rejected");
    expect_out_of_range([&] { (void)data.generated_climb_row(3); },
                        "unreachable generated climb phase three is rejected");
    expect_out_of_range([&] { (void)data.static_climb_row(21); },
                        "static climb row 21 is rejected");
    std::vector<std::uint8_t> short_scene(0x10FF);
    expect_out_of_range(
        [&] { ghostbusters::assets::mirror_sprite_sheets(short_scene); },
        "shared mirror helper rejects a destination below the full scene boundary");
    expect_out_of_range([&] { data.mirror_sprite_sheets(short_scene); },
                        "short mirrored sprite destination is rejected");
    std::vector<std::uint8_t> long_scene(0x1101);
    expect_out_of_range([&] { data.mirror_sprite_sheets(long_scene); },
                        "oversized mirrored sprite destination is rejected");
}

} // namespace

int main()
{
    try {
        const Payload payload = Payload::embedded();
        const ZuulData data(payload);
        test_named_lookup_values(data);
        test_climb_rows(data);
        test_mirrored_sheets(data);
        test_semantic_bounds(data);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
