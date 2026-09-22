#include "assets/payload.hpp"
#include "assets/city_tables.hpp"
#include "game/city_controls.hpp"
#include "game/city_entry.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::CityControlsState;
using ghostbusters::game::CityEntry;
using ghostbusters::game::NoticeScroller;
using Snapshot = NoticeScroller::Snapshot;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

CityControlsState redraw_state(const Payload& payload)
{
    (void)payload;
    CityControlsState state;
    state.state3a = 0x11;
    state.key17 = 0x5A;

    // These are the economic and inventory fields carried through a city
    // redraw.  Distinct values make accidental reconstruction from defaults
    // visible in this raw State-17 boundary fixture.
    state.starting_balance51 = {0x12, 0x34, 0x56};
    state.balance57 = {0x78, 0x90, 0x12};
    state.backup_men3d = 0x05;
    state.backpack_charge3e = 0x67;
    state.pk_low5a = 0x45;
    state.pk_high5b = 0x23;
    state.bait_active68 = 1;
    state.bait69 = 0x06;
    state.empty_traps6b = 0x02;
    state.owned_mask6d = 0xA5;
    state.current_building6e = 0x0D;
    state.countdown7c = 0x19;
    state.pending_alert80 = 0x03;
    state.finale_active81 = 1;

    for (std::size_t index = 0; index < state.map_types_ea28.size(); ++index) {
        state.map_types_ea28[index] =
            ghostbusters::assets::city_tables::kMapTypes[index];
    }

    Snapshot notice;
    notice.phase49 = 0xA1;
    notice.length4a = 0x42;
    notice.position4b = 0x17;
    notice.cached_status4c = 0xC3;
    notice.buffer.fill(0xD4);
    notice.buffer[0x11] = 0xFF;
    notice.row.fill(0x2E);
    state.notices = NoticeScroller(notice);

    // Values are deliberately different from the State-17 startup tables.
    // The pointers remain valid indices into the decoded VIC-bank image.
    state.sprite_control_c0 = {4, 5, 7, 6, 9, 8, 6, 7};
    for (std::size_t sprite = 0; sprite < 8; ++sprite) {
        state.shadow_x_ea46[sprite] = static_cast<std::uint8_t>(0x20 + sprite * 3);
        state.shadow_y_ea47[sprite] = static_cast<std::uint8_t>(0x70 + sprite * 5);
        state.shadow_target_x_ea56[sprite] =
            static_cast<std::uint8_t>(0x90 + sprite * 2);
        state.shadow_target_y_ea57[sprite] =
            static_cast<std::uint8_t>(0xB0 + sprite * 2);
    }
    return state;
}

bool same_notice(const Snapshot& left, const Snapshot& right)
{
    return left.phase49 == right.phase49 && left.length4a == right.length4a &&
           left.position4b == right.position4b &&
           left.cached_status4c == right.cached_status4c &&
           left.buffer == right.buffer && left.row == right.row;
}

void test_state17_handoff_preserves_runtime_state(const Payload& payload)
{
    auto state = redraw_state(payload);
    const auto before = state;
    const auto notice_before = state.notices.snapshot();
    CityEntry::redraw(payload, state);

    check(state.state3a == 0x12 && state.key17 == 0,
          "State 17 redraw returns to State 18 and clears the translated key");
    check(state.starting_balance51 == before.starting_balance51 &&
              state.balance57 == before.balance57 && state.backup_men3d == before.backup_men3d &&
              state.backpack_charge3e == before.backpack_charge3e &&
              state.pk_low5a == before.pk_low5a && state.pk_high5b == before.pk_high5b &&
              state.bait_active68 == before.bait_active68 &&
              state.bait69 == before.bait69 && state.empty_traps6b == before.empty_traps6b &&
              state.owned_mask6d == before.owned_mask6d &&
              state.current_building6e == before.current_building6e &&
              state.countdown7c == before.countdown7c &&
              state.pending_alert80 == before.pending_alert80 &&
              state.finale_active81 == before.finale_active81,
          "city redraw preserves economic, inventory, and runtime resource bytes");
    check(same_notice(state.notices.snapshot(), notice_before),
          "city redraw preserves the complete notice phase and 256-byte buffer");
    check(state.map_types_ea28 == before.map_types_ea28,
          "city redraw preserves the selected dynamic-map type table");
}

void test_dynamic_map_uses_preserved_selectors(const Payload& payload)
{
    auto base = redraw_state(payload);
    auto variant = base;
    const auto original_selector = variant.map_types_ea28[1];
    const auto alternate_selector = static_cast<std::uint8_t>(
        original_selector == 1 ? 2 : 1);
    check(original_selector >= 1 && original_selector <= 6 &&
              alternate_selector >= 1 && alternate_selector <= 6,
          "fixture selects two valid dynamic-map tile selectors");
    variant.map_types_ea28[1] = alternate_selector;

    CityEntry::redraw(payload, base);
    CityEntry::redraw(payload, variant);

    check(base.map_types_ea28[1] == original_selector &&
              variant.map_types_ea28[1] == alternate_selector,
          "redraw leaves each caller's dynamic-map selector unchanged");
    check(base.characters.screen != variant.characters.screen,
          "changing a valid selector changes the rendered dynamic-map graphics");
}

void test_sprite_restore_sources(const Payload& payload)
{
    const auto input = redraw_state(payload);
    auto state = input;
    CityEntry::redraw(payload, state);

    check(state.sprites.pointers == input.sprite_control_c0,
          "State 17 restores sprite pointers from the C0 shadow table");
    check(state.sprites.x == input.shadow_x_ea46 &&
              state.sprites.y == input.shadow_y_ea47,
          "State 17 restores all eight current coordinates from EA46/EA47");

    std::array<std::uint8_t, 8> expected_target_x{};
    std::array<std::uint8_t, 8> expected_target_y{};
    for (std::size_t sprite = 0; sprite < 4; ++sprite) {
        expected_target_x[sprite] = input.shadow_target_x_ea56[sprite];
        expected_target_y[sprite] = input.shadow_target_y_ea57[sprite];
    }
    for (std::size_t sprite = 4; sprite < 8; ++sprite) {
        const auto expected = ghostbusters::assets::city_tables::kArrivingSprites[sprite - 4];
        expected_target_x[sprite] = expected.target_x;
        expected_target_y[sprite] = expected.target_y;
    }
    check(state.sprites.target_x == expected_target_x &&
              state.sprites.target_y == expected_target_y,
          "State 17 restores runtime targets and the four named city-entry targets");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_state17_handoff_preserves_runtime_state(payload);
        test_dynamic_map_uses_preserved_selectors(payload);
        test_sprite_restore_sources(payload);
        std::cout << "native city-redraw tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
