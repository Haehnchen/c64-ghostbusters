#include "assets/city_data.hpp"
#include "assets/city_tables.hpp"
#include "assets/payload.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using ghostbusters::assets::CityData;
using Payload = ghostbusters::assets::Payload;
namespace city_tables = ghostbusters::assets::city_tables;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_out_of_range(Function&& function, const char* message)
{
    try {
        function();
    } catch (const std::out_of_range&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_named_table_values(const Payload& payload, const CityData& city)
{
    const auto charset = payload.asset("city/charset");
    const auto tiles = payload.asset("city/tiles");
    const auto status = payload.asset("city/status_row");
    const auto account = payload.asset("text/account_label");

    for (std::size_t index = 0; index < 30; ++index)
        check(city.map_type(index) == city_tables::kMapTypes[index],
              "map type relocation");

    check(std::equal(city.city_charset().begin(), city.city_charset().end(),
                     charset.begin()),
          "city charset relocation");
    for (std::size_t block = 0; block < 4; ++block) {
        const auto road = city.vertical_road(block);
        check(std::equal(road.begin(), road.end(),
                         city_tables::kVerticalRoad.begin() + block * 11),
              "vertical road relocation");
    }
    check(std::equal(city.status_row().begin(), city.status_row().end(),
                     status.begin()),
          "status row relocation");
    check(std::equal(city.pk_label().begin(), city.pk_label().end(),
                     city_tables::kPkLabel.begin()) &&
              city.pk_separator() == 0x0C,
          "PK label relocation");
    check(std::equal(city.account_label().begin(), city.account_label().end(),
                     account.begin()),
          "account label relocation");

    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        const auto entry = city.entry_sprite(sprite);
        const auto expected = city_tables::kEntrySprites[sprite];
        check(entry.pointer == expected.pointer && entry.x == expected.x &&
                  entry.y == expected.y && entry.target_x == expected.target_x &&
                  entry.target_y == expected.target_y,
              "city entry sprite relocation");
    }

    for (std::size_t x = 0; x < city_tables::kMapDestinations.size(); ++x)
    {
        check(city.map_destination(x) == city_tables::kMapDestinations[x],
              "map destination is a native screen offset");
    }
    check(city.map_destination(0) == -34 &&
              city.map_destination(0) + 3 * 40 == 86,
          "off-screen map base becomes visible after its retained row offset");
    for (std::uint8_t selector = 0; selector < 8; ++selector) {
        for (std::uint8_t row = 0; row < 4; ++row) {
            const auto cells = city.map_row(selector, row);
            check(cells.size() == 5, "map row has five bounded cells");
            for (std::uint8_t column = 0; column < 5; ++column) {
                check(cells[column] == tiles[selector * 20 + row * 5 + column] &&
                          city.map_tile(selector, row, column) == cells[column],
                      "map tile relocation");
            }
        }
    }

    for (std::size_t sprite = 4; sprite < 8; ++sprite) {
        const auto position = city.arriving_sprite(sprite);
        const auto expected = city_tables::kArrivingSprites[sprite - 4U];
        check(position.x == expected.x && position.y == expected.y &&
                  position.target_x == expected.target_x &&
                  position.target_y == expected.target_y,
              "arriving sprite relocation");
    }
    // These are the four roamers, not the preceding player/Zuul records.
    constexpr std::array<std::array<std::uint8_t, 2>, 4> starts{{
        {0, 18}, {168, 50}, {0, 226}, {168, 194}}};
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const auto position = city.arriving_sprite(i + 4);
        check(position.x == starts[i][0] && position.y == starts[i][1] &&
                  position.target_x == 98 && position.target_y == 122,
              "roamers must restart at their own map edges and head for the centre");
    }

    for (const auto selector : std::array<std::uint8_t, 4>{0, 2, 4, 6}) {
        check(city.zuul_route_x_offset(selector) ==
                  city_tables::kZuulRouteXOffsets[selector / 2U] &&
              city.zuul_route_y_offset(selector) ==
                  city_tables::kZuulRouteYOffsets[selector / 2U],
              "Zuul route offset relocation");
    }
    for (std::uint8_t lookup = 0; lookup <= 6; ++lookup)
        check(city.zuul_target_x(lookup) == city_tables::kZuulTargetX[lookup],
              "Zuul target-x relocation");
    for (std::uint8_t lookup = 0; lookup <= 5; ++lookup)
        check(city.zuul_target_y(lookup) == city_tables::kZuulTargetY[lookup],
              "Zuul target-y relocation");

    for (std::size_t sprite = 2; sprite < 4; ++sprite) {
        const auto checkpoint = city.zuul_sprite(sprite);
        const auto expected = city_tables::kZuulSprites[sprite - 2U];
        check(checkpoint.check_x == expected.check_x &&
                  checkpoint.check_y == expected.check_y &&
                  checkpoint.target_x == expected.target_x &&
                  checkpoint.target_y == expected.target_y,
              "Zuul checkpoint relocation");
    }
}

void test_semantic_bounds(const CityData& city)
{
    expect_out_of_range([&] { (void)city.building_color_base(20); }, "invalid color building");
    expect_out_of_range([&] { (void)city.building_phase_color(4); }, "invalid color phase");
    expect_out_of_range([&] { (void)city.next_haunt_phase(4); }, "invalid haunting phase");
    expect_out_of_range([&] { (void)city.difficulty_mask(10); }, "invalid PK digit");
    expect_out_of_range([&] { (void)city.map_type(30); },
                        "map type 30 must be rejected");
    expect_out_of_range([&] { (void)city.vertical_road(4); },
                        "vertical road block four must be rejected");
    expect_out_of_range([&] { (void)city.entry_sprite(8); },
                        "entry sprite eight must be rejected");
    expect_out_of_range([&] { (void)city.map_destination(30); },
                        "map destination column 30 must be rejected");
    expect_out_of_range([&] { (void)city.map_tile(8, 0, 0); },
                        "map tile selector eight must be rejected");
    expect_out_of_range([&] { (void)city.map_tile(0, 4, 0); },
                        "map tile row four must be rejected");
    expect_out_of_range([&] { (void)city.map_tile(0, 0, 5); },
                        "map tile column five must be rejected");
    expect_out_of_range([&] { (void)city.arriving_sprite(3); },
                        "arriving sprite 3 must be rejected");
    expect_out_of_range([&] { (void)city.arriving_sprite(8); },
                        "arriving sprite 8 must be rejected");
    expect_out_of_range([&] { (void)city.zuul_sprite(1); },
                        "Zuul sprite 1 must be rejected");
    expect_out_of_range([&] { (void)city.zuul_sprite(4); },
                        "Zuul sprite 4 must be rejected");
    for (const auto selector : std::array<std::uint8_t, 2>{1, 8}) {
        expect_out_of_range([&] { (void)city.zuul_route_x_offset(selector); },
                            "invalid Zuul route x selector must be rejected");
        expect_out_of_range([&] { (void)city.zuul_route_y_offset(selector); },
                            "invalid Zuul route y selector must be rejected");
    }
    expect_out_of_range([&] { (void)city.zuul_target_x(7); },
                        "Zuul target-x lookup 7 must be rejected");
    expect_out_of_range([&] { (void)city.zuul_target_y(6); },
                        "Zuul target-y lookup 6 must be rejected");
}

void test_city_frame_tables(const CityData& city)
{
    constexpr std::array<std::size_t, 20> bases{
        46,53,60,67,166,173,180,187,366,373,380,387,566,573,580,587,766,773,780,787};
    for (std::size_t i = 0; i < bases.size(); ++i)
        check(city.building_color_base(i) == bases[i], "building color-buffer base");
    constexpr std::array<std::uint8_t, 4> colors{13,12,9,10};
    constexpr std::array<std::uint8_t, 4> next{20,28,248,31};
    for (std::uint8_t i = 0; i < 4; ++i) {
        check(city.building_phase_color(i) == colors[i], "building phase palette");
        check(city.next_haunt_phase(i) == next[i], "haunt transition table");
    }
    constexpr std::array<std::uint8_t, 10> masks{63,31,15,7,3,1,1,1,1,1};
    for (std::uint8_t i = 0; i < masks.size(); ++i)
        check(city.difficulty_mask(i) == masks[i], "PK difficulty mask");
    constexpr std::string_view notice =
        "BACKPACK POWER AT XX% OF MAXIMUM... X EMPTY TRAPS... X MEN LEFT... ";
    const auto actual = city.resource_notice();
    check(actual.size() == notice.size(), "resource notice size");
    for (std::size_t i = 0; i < actual.size(); ++i)
        check(actual[i] == notice[i], "resource notice content");
}

} // namespace

int main()
{
    try {
        const auto payload = Payload::embedded();
        const CityData city(payload);
        constexpr std::array<std::string_view, 10> notice_names{
            "text/notice_0", "text/notice_1", "text/notice_2",
            "text/notice_3", "text/notice_4", "text/notice_5",
            "text/notice_6", "text/notice_7", "text/notice_8",
            "text/notice_9"};
        for (std::uint8_t index = 0; index < 10; ++index) {
            const auto notice = city.notice(index);
            const auto region = payload.asset(notice_names[index]);
            check(std::equal(notice.begin(), notice.end(),
                             region.begin()) &&
                      region.size() == notice.size() + 1 && region.back() == 0xFF,
                  "all ten notices match their named ranges");
        }
        expect_out_of_range([&] { (void)city.notice(10); },
                            "notice index ten is outside the table");
        test_named_table_values(payload, city);
        test_city_frame_tables(city);
        test_semantic_bounds(city);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
