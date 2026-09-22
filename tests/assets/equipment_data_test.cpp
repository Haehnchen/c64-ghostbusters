#include "assets/equipment_data.hpp"
#include "assets/payload.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>

namespace {

using ghostbusters::assets::EquipmentData;
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

std::uint32_t fnv1a(std::span<const std::uint8_t> bytes)
{
    std::uint32_t hash = 2166136261U;
    for (const auto byte : bytes) hash = (hash ^ byte) * 16777619U;
    return hash;
}

void test_fixed_tables(const EquipmentData& data)
{
    constexpr std::array<std::uint8_t, 4> palettes{3, 6, 6, 4};
    constexpr std::array<std::uint8_t, 4> capacities{5, 9, 11, 7};
    constexpr std::array<std::uint8_t, 4> position_bases{0, 24, 48, 72};
    for (std::uint8_t vehicle = 0; vehicle < 4; ++vehicle) {
        check(data.vehicle_multicolor2(vehicle) == palettes[vehicle],
              "vehicle palette has its pinned value");
        check(data.vehicle_capacity(vehicle) == capacities[vehicle],
              "vehicle capacity has its pinned value");
        check(data.vehicle_position_base(vehicle) == position_bases[vehicle],
              "vehicle position base has its pinned value");
    }

    constexpr std::array<std::array<std::uint8_t, 5>, 3> pointers{{
        {{0x33, 0x34, 0x35, 0, 0}},
        {{0x36, 0x38, 0x39, 0, 0}},
        {{0x3A, 0, 0, 0, 0}},
    }};
    for (std::uint8_t category = 0; category < pointers.size(); ++category)
        for (std::size_t item = 0; item < pointers[category].size(); ++item)
            check(data.item_pointer(category, item) == pointers[category][item],
                  "category item pointer has its pinned value");

    constexpr std::array<std::uint8_t, 8> masks{1, 2, 4, 8, 0, 0, 0x20, 0x40};
    constexpr std::array<std::uint8_t, 8> prices{4, 8, 8, 4, 6, 6, 5, 0x80};
    for (std::uint8_t item = 0; item < 8; ++item) {
        const auto pointer = static_cast<std::uint8_t>(0x33U + item);
        check(data.ownership_mask(pointer) == masks[item],
              "ownership mask has its pinned value");
        check(data.price_middle_bcd(pointer) == prices[item],
              "price byte has its pinned value");
    }

    constexpr std::array<EquipmentData::SpritePosition, 8> positions{{
        {40, 82}, {16, 82}, {16, 82}, {20, 82},
        {20, 114}, {20, 146}, {20, 171}, {20, 194},
    }};
    for (std::size_t sprite = 0; sprite < positions.size(); ++sprite) {
        const auto actual = data.initial_sprite_position(sprite);
        check(actual.x == positions[sprite].x && actual.y == positions[sprite].y,
              "initial sprite position has its pinned value");
    }

    constexpr std::array<std::array<std::uint8_t, 4>, 3> graphics{{
        {{1, 2, 3, 0}}, {{4, 5, 6, 0}}, {{7, 0, 0, 0}},
    }};
    for (std::uint8_t category = 0; category < graphics.size(); ++category)
        for (std::size_t row = 0; row < graphics[category].size(); ++row)
            check(data.carried_graphic(category, row) == graphics[category][row],
                  "carried graphic has its pinned value");

    constexpr std::array<std::uint8_t, 3> limits{9, 9, 1};
    for (std::uint8_t category = 0; category < limits.size(); ++category)
        check(data.category_state_limit(category) == limits[category],
              "category state limit has its pinned value");

    constexpr std::array<std::uint8_t, 10> label{
        0x2D, 0x2D, 0x03, 0x12, 0x05, 0x04, 0x09, 0x14, 0x2D, 0x2D};
    check(std::equal(data.balance_label().begin(), data.balance_label().end(),
                     label.begin()),
          "balance label has its pinned bytes");
    check(data.forklift_x(0) == 0x28 && data.forklift_x(1) == 0x4C,
          "forklift phases have their pinned x coordinates");
}

void test_bounded_spans_and_checksums(const EquipmentData& data)
{
    check(data.scene_graphics().size() == 3776 &&
              fnv1a(data.scene_graphics()) == 0xD7A8084BU,
          "shared sprite graphics have their pinned size and checksum");

    std::array<std::uint8_t, EquipmentData::kCarriedPositionCount> target_x{};
    std::array<std::uint8_t, EquipmentData::kCarriedPositionCount> target_y{};
    for (std::size_t index = 0; index < target_x.size(); ++index) {
        target_x[index] = data.carried_target_x(index);
        target_y[index] = data.carried_target_y(index);
    }
    check(fnv1a(target_x) == 0x7BC3ED63U &&
              fnv1a(target_y) == 0x51B43A9DU,
          "carried-position tables have pinned checksums");

    std::array<std::uint8_t, 0x3B> colors{};
    for (std::size_t pointer = 0; pointer < colors.size(); ++pointer)
        colors[pointer] = data.sprite_color(static_cast<std::uint8_t>(pointer));
    check(fnv1a(colors) == 0xA9E42486U,
          "equipment sprite-color range has its pinned checksum");

    constexpr std::array<std::size_t, 3> script_sizes{283, 283, 289};
    constexpr std::array<std::uint32_t, 3> script_hashes{
        0x9AE56D2BU, 0x4723230DU, 0x9B4EBD12U};
    constexpr std::array<char, 3> first_letters{'M', 'C', 'S'};
    for (std::uint8_t category = 0; category < 3; ++category) {
        const auto script = data.category_script(category);
        check(script.size() == script_sizes[category] && script.front() == first_letters[category] &&
                  script.back() == 0xFF && fnv1a(script) == script_hashes[category],
              "category script is complete, terminated and pinned");
    }

    constexpr std::array<std::uint8_t, 32> notice{
        'Y', 'O', 'U', 'R', ' ', 'C', 'A', 'R', ' ', 'I', 'S', ' ', 'L', 'O', 'A', 'D',
        'E', 'D', ' ', 'T', 'O', ' ', 'C', 'A', 'P', 'A', 'C', 'I', 'T', 'Y', '.', ' '};
    check(data.capacity_notice().size() == notice.size() &&
              std::equal(data.capacity_notice().begin(),
                         data.capacity_notice().end(), notice.begin()) &&
              fnv1a(data.capacity_notice()) == 0xD087FA1CU,
          "capacity notice has its pinned bytes and excludes the terminator");
}

void test_invalid_indices(const EquipmentData& data)
{
    expect_out_of_range([&] { (void)data.vehicle_multicolor2(4); },
                        "palette rejects vehicle four");
    expect_out_of_range([&] { (void)data.vehicle_capacity(4); },
                        "capacity rejects vehicle four");
    expect_out_of_range([&] { (void)data.vehicle_position_base(4); },
                        "position base rejects vehicle four");
    expect_out_of_range([&] { (void)data.category_script(3); },
                        "script rejects category three");
    expect_out_of_range([&] { (void)data.item_pointer(3, 0); },
                        "item pointer rejects category three");
    expect_out_of_range([&] { (void)data.item_pointer(0, 5); },
                        "item pointer rejects item five");
    expect_out_of_range([&] { (void)data.initial_sprite_position(8); },
                        "position rejects sprite eight");
    expect_out_of_range([&] { (void)data.carried_graphic(0, 4); },
                        "carried graphic rejects row four");
    expect_out_of_range([&] { (void)data.category_state_limit(3); },
                        "state limit rejects category three");
    expect_out_of_range([&] { (void)data.ownership_mask(0x32); },
                        "ownership mask rejects pointer below items");
    expect_out_of_range([&] { (void)data.price_middle_bcd(0x3B); },
                        "price rejects pointer above items");
    expect_out_of_range([&] { (void)data.carried_target_x(96); },
                        "target x rejects index 96");
    expect_out_of_range([&] { (void)data.carried_target_y(96); },
                        "target y rejects index 96");
    expect_out_of_range([&] { (void)data.forklift_x(2); },
                        "forklift x rejects phase two");
    expect_out_of_range([&] { (void)data.sprite_color(0x3B); },
                        "sprite color rejects pointer above shop graphics");
}

} // namespace

int main()
{
    try {
        const auto payload = Payload::embedded();
        const EquipmentData data(payload);
        const ghostbusters::assets::NoticeData notices(payload);
        check(data.capacity_notice().data() == notices.notice(7).data() &&
                  data.capacity_notice().size() == notices.notice(7).size(),
              "shop capacity notice shares the native notice span");
        test_fixed_tables(data);
        test_bounded_spans_and_checksums(data);
        test_invalid_indices(data);
        if (failures != 0) return 1;
        std::cout << "equipment data tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
