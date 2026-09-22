#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/city_entry.hpp"
#include "game/pal_counter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::AccountBalanceBytes;
using ghostbusters::game::CityEntry;
using ghostbusters::game::EquipmentInput;
using ghostbusters::game::EquipmentSelection;
using ghostbusters::video::CharacterFrame;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void finish_script(EquipmentSelection& selection, std::uint8_t& irq)
{
    for (unsigned tick = 0; tick < 32 && selection.stage() != EquipmentSelection::Stage::ready;
         ++tick) {
        selection.tick(irq = ghostbusters::game::advance_pal_counter(irq));
    }
    check(selection.stage() == EquipmentSelection::Stage::ready,
          "equipment script did not reach its state-15 boundary");
}

void settle_after_tick(EquipmentSelection& selection, std::uint8_t& irq, EquipmentInput input = {})
{
    selection.tick(irq = ghostbusters::game::advance_pal_counter(irq), input);
    selection.tick(irq = ghostbusters::game::advance_pal_counter(irq));
    for (unsigned tick = 0;
         tick < 256 && (selection.sprites().x != selection.sprites().target_x ||
                        selection.sprites().y != selection.sprites().target_y);
         ++tick) {
        selection.tick(irq = ghostbusters::game::advance_pal_counter(irq));
    }
    check(selection.sprites().x == selection.sprites().target_x &&
              selection.sprites().y == selection.sprites().target_y,
          "equipment sprites did not settle");
}

void press_fire(EquipmentSelection& selection, std::uint8_t& irq)
{
    selection.tick(irq = ghostbusters::game::advance_pal_counter(irq), {0xEF, 0});
    selection.tick(irq = ghostbusters::game::advance_pal_counter(irq));
}

void move_to_trap(EquipmentSelection& selection, std::uint8_t& irq)
{
    while ((selection.state5f() >> 2U) < 1) settle_after_tick(selection, irq, {0xFD, 0});
}

void buy_trap(EquipmentSelection& selection, std::uint8_t& irq)
{
    move_to_trap(selection, irq);
    press_fire(selection, irq);
    settle_after_tick(selection, irq);
    settle_after_tick(selection, irq, {0xF7, 0});
    press_fire(selection, irq);
}

void test_state16_boundary_and_handoff(const Payload& payload)
{
    std::uint8_t irq = 0;
    CharacterFrame prior;
    EquipmentSelection shop(payload, prior, 2, {0x12, 0x34, 0x56}, 0x44, 1);
    finish_script(shop, irq);
    shop.tick(irq = ghostbusters::game::advance_pal_counter(irq));

    const auto shop_frame = shop.characters();
    const auto shop_sprites = shop.sprites();
    CityEntry entry(payload, shop);
    check(entry.stage() == CityEntry::Stage::leaving_shop &&
              entry.characters().screen == shop_frame.screen,
          "constructor must expose the unmodified state-16 entry boundary");
    entry.tick();

    check(entry.stage() == CityEntry::Stage::preparing_city,
          "first city-entry tick must stop at original state 17");
    check(std::all_of(entry.characters().screen.begin(), entry.characters().screen.begin() + 0x3C0,
                      [](std::uint8_t value) { return value == 0; }) &&
              std::all_of(entry.characters().colors.begin(),
                          entry.characters().colors.begin() + 0x3C0,
                          [](std::uint8_t value) { return value == 0; }) &&
              std::equal(entry.characters().screen.begin() + 0x3C0,
                         entry.characters().screen.end(), shop_frame.screen.begin() + 0x3C0) &&
              std::equal(entry.characters().colors.begin() + 0x3C0,
                         entry.characters().colors.end(), shop_frame.colors.begin() + 0x3C0),
          "state 16 clears the equipment character layer");
    check(std::equal(entry.saved_vehicle_charset().begin(),
                     entry.saved_vehicle_charset().end(),
                     shop_frame.charset.begin() + 0x200),
          "state 16 saves all 1536 purchased-vehicle charset bytes");
    check(entry.sprites().pointers == shop_sprites.pointers &&
              entry.sprites().target_x == shop_sprites.target_x &&
              entry.sprites().target_y == shop_sprites.target_y &&
              std::all_of(entry.sprites().x.begin(), entry.sprites().x.end(),
                          [](std::uint8_t value) { return value == 0; }) &&
              std::all_of(entry.sprites().y.begin(), entry.sprites().y.end(),
                          [](std::uint8_t value) { return value == 0; }),
          "state 16 clears only active sprite coordinates at its checkpoint");
    check(entry.balance() == AccountBalanceBytes{0x12, 0x34, 0x56} &&
              entry.vehicle() == 2 && entry.category() == 1 && entry.owned_mask() == 0x44,
          "state 16 preserves the equipment handoff fields");
    check(entry.take_music_reset_request() && !entry.take_music_reset_request(),
          "state 16 exposes its music-channel reset once");
}

void test_state17_city_frame_and_sprites(const Payload& payload)
{
    const auto charset = payload.asset("city/charset");
    const auto status = payload.asset("city/status_row");
    std::uint8_t irq = 0;
    CharacterFrame prior;
    EquipmentSelection shop(payload, prior, 0, {0, 0x50, 0}, 0, 1);
    finish_script(shop, irq);
    CityEntry entry(payload, shop);
    entry.tick();
    const auto first_charset_pages = [&] {
        std::array<std::uint8_t, 512> value{};
        std::copy_n(entry.characters().charset.begin(), value.size(), value.begin());
        return value;
    }();
    entry.tick();

    check(entry.stage() == CityEntry::Stage::ready,
          "second city-entry tick must stop at original state 18");
    check(std::equal(first_charset_pages.begin(), first_charset_pages.end(),
                     entry.characters().charset.begin()) &&
              std::equal(charset.begin(), charset.end(),
                         entry.characters().charset.begin() + 0x200),
          "state 17 replaces exactly six charset pages");
    check(entry.characters().background == 0x0C &&
              entry.characters().multicolor1 == 7 && entry.characters().multicolor2 == 0,
          "state 17 installs the original city character colors");
    check(std::equal(status.begin(), status.end(),
                     entry.characters().screen.begin()) &&
              std::all_of(entry.characters().screen.begin() + 0x348,
                          entry.characters().screen.begin() + 0x370,
                          [](std::uint8_t value) { return value == 0xA9; }) &&
              entry.characters().screen[0x1CC] == 0x1A &&
              entry.characters().screen[0x32E] == 0x07,
          "state 17 applies the original map overlays");
    check(entry.characters().colors[0] == 0x0D &&
              entry.characters().colors[0x348] == 6 &&
              entry.characters().colors[0x36F] == 6 &&
              entry.characters().colors[0x370] == 1 &&
              entry.characters().colors[0x396] == 1 &&
              entry.characters().colors[0x397] == 0,
          "state 17 reproduces the overlapping color fills");

    const std::array<std::uint8_t, 8> pointers{4, 5, 7, 6, 8, 9, 8, 9};
    const std::array<std::uint8_t, 8> x{0x47, 0x47, 0, 0, 0, 0xA8, 0, 0xA8};
    const std::array<std::uint8_t, 8> y{0xBA, 0xBA, 0x42, 0xBA, 0x12, 0x32, 0xE2, 0xC2};
    const std::array<std::uint8_t, 8> target_x{0x47, 0x47, 0x1D, 0x1D, 0x62, 0x62, 0x62, 0x62};
    const std::array<std::uint8_t, 8> target_y{0xBA, 0xBA, 0x42, 0xBA, 0x7A, 0x7A, 0x7A, 0x7A};
    check(entry.sprites().pointers == pointers && entry.sprites().x == x &&
              entry.sprites().y == y && entry.sprites().target_x == target_x &&
              entry.sprites().target_y == target_y,
          "state 17 activates the eight original city sprites");
    check(entry.sprites().priority_mask == 0 && entry.sprites().multicolor_mask == 2 &&
              entry.sprites().x_expand_mask == 0 && entry.sprites().y_expand_mask == 0 &&
              entry.sprites().shared_multicolor_1 == 0 &&
              entry.sprites().shared_multicolor_2 == 0 &&
              entry.sprites().colors == std::array<std::uint8_t, 8>{1, 2, 7, 0, 7, 7, 7, 7},
          "state 17 installs its sprite VIC masks and pointer-derived colors");
}

void test_purchase_and_notice_handoff(const Payload& payload)
{
    std::uint8_t irq = 0;
    CharacterFrame prior;
    EquipmentSelection shop(payload, prior, 0, {1, 0, 0}, 0, 1);
    finish_script(shop, irq);
    shop.tick(irq = ghostbusters::game::advance_pal_counter(irq));
    for (unsigned purchase = 0; purchase < 5; ++purchase) buy_trap(shop, irq);
    move_to_trap(shop, irq);
    press_fire(shop, irq);
    settle_after_tick(shop, irq);
    settle_after_tick(shop, irq, {0xF7, 0});
    press_fire(shop, irq);
    check(!shop.pending_notice().empty(), "capacity setup did not queue its notice");

    CityEntry entry(payload, shop);
    const auto notice = shop.pending_notice();
    entry.tick();
    entry.tick();
    check(entry.purchase_count() == 5 && entry.traps6a() == 5 && entry.traps6b() == 5 &&
              entry.pending_notice() == notice,
          "states 16 and 17 preserve cargo and the deferred capacity notice");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_state16_boundary_and_handoff(payload);
        test_state17_city_frame_and_sprites(payload);
        test_purchase_and_notice_handoff(payload);
        std::cout << "native city-entry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
