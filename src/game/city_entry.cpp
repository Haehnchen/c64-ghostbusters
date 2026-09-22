#include "game/city_entry.hpp"

#include "game/title_screen.hpp"
#include "game/city_controls.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

constexpr std::size_t kVehicleCharsetOffset = 0x0200;

} // namespace

CityEntry::CityEntry(const assets::Payload& payload, const EquipmentSelection& shop)
    : city_data_(payload), characters_(shop.characters()), sprites_(shop.sprites()),
      balance_(shop.balance()), vehicle_(shop.vehicle()), category_(shop.category()),
      state5f_(shop.state5f()), carried_slot60_(shop.carried_slot60()),
      carried_graphic61_(shop.carried_graphic61()), purchase_count_(shop.purchase_count62()),
      bait_(shop.bait69()), traps6a_(shop.traps6a()), traps6b_(shop.traps6b()),
      owned_mask_(shop.owned_mask()), fire_latch11_(shop.fire_latch11()),
      pending_notice_(shop.pending_notice())
{
    scene_data_.assign(city_data_.scene_graphics().begin(),
                       city_data_.scene_graphics().end());
}

void CityEntry::tick()
{
    if (stage_ == Stage::leaving_shop) {
        leave_shop();
    } else if (stage_ == Stage::preparing_city) {
        prepare_city();
    }
}

bool CityEntry::take_music_reset_request() noexcept
{
    return std::exchange(music_reset_requested_, false);
}

void CityEntry::leave_shop()
{
    // $7A9C-$7AB8 stages the city coordinates outside the active zero-page
    // sprite arrays. The active current coordinates are cleared here; state
    // 17 installs the staged values.
    sprites_.x.fill(0);
    sprites_.y.fill(0);

    std::copy_n(characters_.charset.begin() + kVehicleCharsetOffset,
                saved_vehicle_charset_.size(), saved_vehicle_charset_.begin());
    // $95B9 clears four overlapping pages based at $5400/$5500/$5600/$56C0.
    // The final 64 bytes at $57C0-$57FF and $DBC0-$DBFF survive.
    std::fill_n(characters_.screen.begin(), 0x3C0, 0);
    std::fill_n(characters_.colors.begin(), 0x3C0, 0);
    state5e_ = 0;
    music_reset_requested_ = true;
    stage_ = Stage::preparing_city;
}

CityEntry::CityEntry(const assets::Payload& payload, const CityControlsState& state)
    : city_data_(payload), characters_(state.characters), sprites_(state.sprites),
      scene_data_(city_data_.scene_graphics().begin(),
                  city_data_.scene_graphics().end())
{
}

void CityEntry::redraw(const assets::Payload& payload, CityControlsState& state)
{
    CityEntry redraw(payload, state);
    redraw.prepare_city(&state);
    state.characters = std::move(redraw.characters_);
    state.sprites = std::move(redraw.sprites_);
    state.key17 = 0;
    state.state3a = 0x12;
}

void CityEntry::prepare_city(const CityControlsState* restored)
{
    std::fill_n(characters_.screen.begin(), 0x3C0, 0);
    std::fill_n(characters_.colors.begin(), 0x3C0, 0);
    // clear_screen_and_color's state-17 tail colors the future scrolling row.
    std::fill_n(characters_.colors.begin() + 0x370, 0x27, 1);

    characters_.background = 0x0C;
    characters_.multicolor1 = 0x07;
    characters_.multicolor2 = 0;
    characters_.multicolor = true;
    std::ranges::copy(city_data_.city_charset(),
                      characters_.charset.begin() + kVehicleCharsetOffset);

    // $D800/$D900/$DA00/$DA6E, each for one complete page.
    std::fill_n(characters_.colors.begin(), 0x300, 0x0D);
    std::fill_n(characters_.colors.begin() + 0x26E, 0x100, 0x0D);
    build_map_tiles(restored);

    std::fill_n(characters_.screen.begin() + 0x348, 40, 0xA9);
    std::fill_n(characters_.colors.begin() + 0x348, 40, 6);
    for (std::size_t block = 0; block < 4; ++block) {
        std::ranges::copy(city_data_.vertical_road(block),
                          characters_.screen.begin() + 0x078 + block * 0x0C8);
    }
    std::ranges::copy(city_data_.status_row(), characters_.screen.begin());
    std::ranges::copy(city_data_.pk_label(), characters_.screen.begin() + 0x1CC);
    std::ranges::copy(city_data_.account_label(), characters_.screen.begin() + 0x32E);
    std::fill_n(characters_.colors.begin() + 0x1CC, 3, 1);
    std::fill_n(characters_.colors.begin() + 0x32E, 3, 1);
    characters_.screen[0x1CF] = city_data_.pk_separator();
    characters_.colors[0x1CF] = 1;

    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        const auto entry = city_data_.entry_sprite(sprite);
        sprites_.pointers[sprite] = restored ? restored->sprite_control_c0[sprite] :
            entry.pointer;
        sprites_.x[sprite] = restored ? restored->shadow_x_ea46[sprite] :
            entry.x;
        sprites_.y[sprite] = restored ? restored->shadow_y_ea47[sprite] :
            entry.y;
        sprites_.target_x[sprite] = restored && sprite < 4 ? restored->shadow_target_x_ea56[sprite] :
            entry.target_x;
        sprites_.target_y[sprite] = restored && sprite < 4 ? restored->shadow_target_y_ea57[sprite] :
            entry.target_y;
    }
    sprites_.priority_mask = 0;
    sprites_.multicolor_mask = 0x02;
    if (!restored) {
        sprites_.shared_multicolor_1 = 0;
        sprites_.shared_multicolor_2 = 0;
    }
    sprites_.x_expand_mask = 0;
    sprites_.y_expand_mask = 0;
    sprites_.x_high_mask = 0;
    resolve_city_sprite_visuals();
    stage_ = Stage::ready;
}

void CityEntry::build_map_tiles(const CityControlsState* restored)
{
    for (int x = 29; x >= 0; --x) {
        for (int y = 3; y >= 0; --y) {
            if ((x < 4 || x >= 29) && y < 2) continue;
            if (x >= 16 && x < 22 && y >= 2) continue;

            const auto destination = city_data_.map_destination(
                static_cast<std::size_t>(x)) + static_cast<std::ptrdiff_t>(y) * 40;
            const auto selector = restored ? restored->map_types_ea28[x] :
                city_data_.map_type(static_cast<std::size_t>(x));
            for (std::uint8_t column = 0; column < 5; ++column) {
                const auto cell = destination + column;
                if (cell >= 0 &&
                    static_cast<std::size_t>(cell) < characters_.screen.size()) {
                    characters_.screen[static_cast<std::size_t>(cell)] = city_data_.map_tile(
                        selector, static_cast<std::uint8_t>(y), column);
                }
            }
        }
    }
}

void CityEntry::resolve_city_sprite_visuals()
{
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        const auto pointer = sprites_.pointers[sprite];
        const auto source = scene_data_.begin() + static_cast<std::size_t>(pointer) * 64U;
        std::copy_n(source, 64, sprites_.bitmap_data[sprite].begin());
        sprites_.colors[sprite] = city_data_.sprite_color(pointer);
    }
}

} // namespace ghostbusters::game
