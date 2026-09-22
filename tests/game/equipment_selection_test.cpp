#include "assets/payload.hpp"
#include "assets/capture_tables.hpp"
#include "assets/equipment_tables.hpp"
#include "game/equipment_selection.hpp"
#include "game/pal_counter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::AccountBalanceBytes;
using ghostbusters::game::EquipmentSelection;
using ghostbusters::game::EquipmentInput;
using ghostbusters::game::SoundEvent;
using ghostbusters::game::SoundEvents;
using ghostbusters::video::CharacterFrame;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct TestClock {
    std::uint8_t current = 0;

    void tick(EquipmentSelection& selection, EquipmentInput input = {})
    {
        current = ghostbusters::game::advance_pal_counter(current);
        selection.tick(current, input);
    }
};

void finish_script(EquipmentSelection& selection, TestClock& clock)
{
    for (unsigned tick = 0; tick < 32 && selection.stage() != EquipmentSelection::Stage::ready;
         ++tick) {
        clock.tick(selection);
    }
    check(selection.stage() == EquipmentSelection::Stage::ready,
          "equipment script did not reach the ready stage");
}

void settle_after_tick(EquipmentSelection& selection, TestClock& clock,
                       EquipmentInput input = {})
{
    clock.tick(selection, input);
    // State 15 derives targets before it applies joystick changes. One neutral
    // update is therefore required before the new phase/row target is visible.
    clock.tick(selection);
    for (unsigned tick = 0;
         tick < 256 && (selection.sprites().x != selection.sprites().target_x ||
                        selection.sprites().y != selection.sprites().target_y);
         ++tick) {
        clock.tick(selection);
    }
    check(selection.sprites().x == selection.sprites().target_x &&
              selection.sprites().y == selection.sprites().target_y,
          "equipment sprites did not reach their original targets");
}

void move_to_row(EquipmentSelection& selection, TestClock& clock, std::uint8_t row)
{
    for (unsigned tick = 0; tick < 8 && (selection.state5f() >> 2U) < row; ++tick)
        settle_after_tick(selection, clock, {0xFD, 0});
    for (unsigned tick = 0; tick < 8 && (selection.state5f() >> 2U) > row; ++tick)
        settle_after_tick(selection, clock, {0xFE, 0});
    check((selection.state5f() >> 2U) == row, "forklift did not reach requested item row");
}

void press_fire(EquipmentSelection& selection, TestClock& clock)
{
    clock.tick(selection, {0xEF, 0});
    clock.tick(selection);
}

void buy_current_row(EquipmentSelection& selection, TestClock& clock)
{
    press_fire(selection, clock);
    check(selection.carried_slot60() != 0, "fire did not pick up the selected equipment");
    settle_after_tick(selection, clock);
    settle_after_tick(selection, clock, {0xF7, 0});
    check((selection.state5f() & 3U) == 1, "forklift did not reach its purchase phase");
    press_fire(selection, clock);
}

std::uint8_t screen_code(char value)
{
    const auto byte = static_cast<std::uint8_t>(value);
    return byte == 0x20 ? 0 : byte >= 0x40 ? static_cast<std::uint8_t>(byte - 0x40) : byte;
}

void check_text(const CharacterFrame& frame, std::size_t row, std::size_t column,
                const char* text)
{
    for (std::size_t index = 0; text[index] != '\0'; ++index) {
        check(frame.screen[row * 40 + column + index] == screen_code(text[index]),
              "equipment screen text differs from the original script");
    }
}

void test_original_tables(const Payload& payload)
{
    (void)payload;
    constexpr std::array<std::uint8_t, 24> item_blocks{
        0x33, 0x34, 0x35, 0, 0, 0, 0, 0,
        0x36, 0x38, 0x39, 0, 0, 0, 0, 0,
        0x3A, 0, 0, 0, 0, 0, 0, 0};
    check(ghostbusters::assets::equipment_tables::kCategoryItems == item_blocks,
          "equipment item table has its named native values");
    constexpr std::array<std::uint8_t, 16> positions{
        40, 82, 16, 82, 16, 82, 20, 82,
        20, 114, 20, 146, 20, 171, 20, 194,
    };
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const auto& position = ghostbusters::assets::equipment_tables::kInitialPositions[index / 2];
        check(position[index & 1U] == positions[index],
              "equipment coordinate table has its native values");
    }
}

void test_categories_and_graphics(const Payload& payload)
{
    const std::array<const char*, 3> headings{
        "MONITORING EQUIPMENT:", "CAPTURE EQUIPMENT:", "STORAGE EQUIPMENT:",
    };
    const std::array<const char*, 3> first_items{
        "PK ENERGY DETECTOR", "GHOST BAIT", "PORTABLE LASER",
    };
    const std::array<const char*, 3> first_prices{"$400", "$400", "$8000"};
    const std::array<std::size_t, 3> first_price_columns{31, 31, 30};
    const std::array<std::array<std::uint8_t, 8>, 3> pointers{{
        {1, 3, 3, 0x33, 0x34, 0x35, 0, 0},
        {1, 3, 3, 0x36, 0x38, 0x39, 0, 0},
        {1, 3, 3, 0x3A, 0, 0, 0, 0},
    }};
    const AccountBalanceBytes balance{0x09, 0x87, 0x65};

    for (std::uint8_t category = 0; category < 3; ++category) {
        CharacterFrame previous;
        previous.charset.fill(0x5A);
        EquipmentSelection selection(payload, previous, 2, balance, 0, category);
        check(selection.stage() == EquipmentSelection::Stage::initial,
              "equipment category begins while its original script is active");
        check(selection.sprites().pointers[1] == 0 && selection.sprites().pointers[2] == 0 &&
                  selection.sprites().x_expand_mask == 0,
              "state 14 preserves inherited paired-fork pointers before state 15 runs");
        check(std::any_of(selection.characters().charset.begin() + 0x0200,
                          selection.characters().charset.end(),
                          [](std::uint8_t value) { return value != 0x5A; }) &&
                  selection.characters().screen[4 * 40 + 18] == 0x40 &&
                  selection.characters().colors[4 * 40 + 18] == 8,
                  "equipment entry reconstructs the purchased vehicle graphics");
        TestClock clock;
        finish_script(selection, clock);

        check_text(selection.characters(), 1, 1, headings[category]);
        check_text(selection.characters(), 3, 1, first_items[category]);
        check_text(selection.characters(), 3, first_price_columns[category], first_prices[category]);
        check_text(selection.characters(), 21, 1, "USE JOYSTICK TO CONTROL FORKLIFT.");
        check(selection.sprites().pointers == pointers[category],
              "equipment category selects its original item sprite pointers");
        check(selection.balance() == balance && selection.owned_mask() == 0 &&
                  selection.vehicle() == 2 && selection.category() == category,
              "equipment entry preserves account and selection state");
        check(selection.characters().multicolor1 == 1 &&
                  selection.characters().multicolor2 ==
                      ghostbusters::assets::equipment_tables::kVehicleMulticolor2[2],
              "equipment entry selects the purchased vehicle colors");
        check(selection.take_sound_events() == SoundEvents{SoundEvent::equipment_motor_stop},
              "category FF dispatches the settled State15 motor stop in the same frame");
    }
}

void testExplicitIrqPhases(const Payload& payload)
{
    CharacterFrame previous;
    previous.charset.fill(0x5A);

    std::array<std::uint8_t, 1024> reference_screen{};
    std::array<std::uint8_t, 1024> reference_colors{};
    // PAL $08 reaches these four values; their low two bits are phases 0..3.
    constexpr std::array<std::uint8_t, 4> phases{4, 5, 6, 7};
    for (std::size_t phase_index = 0; phase_index < phases.size(); ++phase_index) {
        const auto phase = phases[phase_index];
        EquipmentSelection selection(payload, previous, 0, {0x09, 0x87, 0x65}, 0, 0);
        for (unsigned tick = 0;
             tick < 32 && selection.stage() == EquipmentSelection::Stage::initial;
             ++tick) {
            // Equipment's entry script uses cadence mask zero, so every
            // caller-supplied IRQ phase consumes the same 40-byte batch.
            selection.tick(phase);
        }
        check(selection.stage() == EquipmentSelection::Stage::ready,
              "equipment mask-zero script did not reach the ready stage");
        check(selection.take_sound_events() == SoundEvents{SoundEvent::equipment_motor_stop},
              "all phases finish the silent script and dispatch one motor stop");

        if (phase_index == 0) {
            reference_screen = selection.characters().screen;
            reference_colors = selection.characters().colors;
        } else {
            check(selection.characters().screen == reference_screen &&
                      selection.characters().colors == reference_colors,
                  "equipment mask-zero text output must match in IRQ phases 0..3");
        }
    }
}

void test_sprite_initialization_and_owned_filter(const Payload& payload)
{
    CharacterFrame previous;
    EquipmentSelection selection(payload, previous, 0, {0x01, 0x23, 0x45}, 0x68, 1);
    const std::array<std::uint8_t, 8> expected_pointers{1, 0, 0, 0, 0x38, 0, 0, 0};
    const std::array<std::uint8_t, 8> expected_x{40, 16, 16, 20, 20, 20, 20, 20};
    const std::array<std::uint8_t, 8> expected_y{82, 82, 82, 82, 114, 146, 171, 194};

    check(selection.sprites().pointers == expected_pointers,
          "owned capture items are removed with the original sparse masks");
    check(selection.sprites().x == expected_x && selection.sprites().target_x == expected_x &&
              selection.sprites().y == expected_y && selection.sprites().target_y == expected_y,
          "all eight current and target coordinates start at the original tables");
    check(selection.sprites().y_expand_mask == 0 &&
              selection.sprites().priority_mask == 0 &&
              selection.sprites().multicolor_mask == 0xFF &&
              selection.sprites().x_expand_mask == 0,
          "state 14 exposes the three VIC sprite masks it initializes");
    check(selection.sprites().enabled_mask == 0xFF &&
              selection.sprites().x_high_mask == 0 &&
              selection.sprites().shared_multicolor_1 == 1 &&
              selection.sprites().shared_multicolor_2 == 0 &&
              selection.sprites().colors[0] == 7 &&
              selection.sprites().colors[4] == 2,
          "equipment sprites expose the IRQ-derived enable and color state");
    check(selection.sprites().bitmap_data[0][14] == 0x50 &&
              selection.sprites().bitmap_data[1][60] == 0 &&
              selection.sprites().bitmap_data[4][40] == 0xFF,
          "sprite pointers resolve against the decoded $4000 startup image");
    check(selection.balance() == AccountBalanceBytes{0x01, 0x23, 0x45} &&
              selection.owned_mask() == 0x68,
          "owned filtering does not debit or otherwise mutate persistent state");
}

void test_rejects_invalid_indices(const Payload& payload)
{
    CharacterFrame previous;
    bool vehicle_rejected = false;
    bool category_rejected = false;
    try {
        EquipmentSelection invalid(payload, previous, 4, {}, 0, 0);
    } catch (const std::out_of_range&) {
        vehicle_rejected = true;
    }
    try {
        EquipmentSelection invalid(payload, previous, 0, {}, 0, 3);
    } catch (const std::out_of_range&) {
        category_rejected = true;
    }
    check(vehicle_rejected && category_rejected,
          "equipment entry rejects indexes absent from the original tables");
}

void test_state15_navigation_and_category_keys(const Payload& payload)
{
    CharacterFrame previous;
    EquipmentSelection selection(payload, previous, 0, {0, 0x50, 0}, 0, 0);
    TestClock clock;
    finish_script(selection, clock);

    // The category's FF falls through to common balance setup and State15.
    check(selection.script_active() && selection.column() == 28 && selection.row() == 1,
          "category terminator starts the runtime balance script in the same frame");
    check(selection.sprites().pointers[1] == 3 && selection.sprites().pointers[2] == 3 &&
              selection.sprites().x_expand_mask == 0x06 && selection.fire_latch11() == 0x10,
          "first state-15 update installs the paired fork and fire latch");
    check(selection.take_sound_events() == SoundEvents{SoundEvent::equipment_motor_stop},
          "settled state-15 update follows the original motor-stop call");

    clock.tick(selection, {0xFD, 0});
    check(selection.take_sound_events() == SoundEvents{SoundEvent::equipment_motor_stop},
          "row input changes state after the settled motor branch");
    clock.tick(selection);
    check(selection.take_sound_events() == SoundEvents{SoundEvent::equipment_motor_start},
          "following update starts the motor while moving toward the new row");
    settle_after_tick(selection, clock, {0xFE, 0});

    for (unsigned attempt = 0; attempt < 4; ++attempt)
        settle_after_tick(selection, clock, {0xFD, 0});
    check(selection.state5f() == 8,
          "monitoring category clamps downward movement to its third item row");
    move_to_row(selection, clock, 0);
    clock.tick(selection, {0xFF, '2'});
    check(selection.stage() == EquipmentSelection::Stage::initial && selection.category() == 1 &&
              selection.state5f() == 0 && selection.carried_slot60() == 0 &&
              selection.sprites().x_expand_mask == 0x06,
          "number key changes category through a fresh state-14 script");
    finish_script(selection, clock);
    check_text(selection.characters(), 1, 1, "CAPTURE EQUIPMENT:");
    clock.tick(selection, {0xFF, 'E'});
    check(selection.stage() == EquipmentSelection::Stage::ready,
          "E remains gated until at least one trap has been bought");
}

void test_purchase_economy_effects_and_exit(const Payload& payload)
{
    CharacterFrame previous;
    EquipmentSelection selection(payload, previous, 0, {0, 0x20, 0}, 0, 0);
    TestClock clock;
    finish_script(selection, clock);
    clock.tick(selection);
    const auto charset_before = selection.characters().charset;
    buy_current_row(selection, clock);
    check(selection.balance() == AccountBalanceBytes{0, 0x16, 0} &&
              selection.owned_mask() == 0x01 && selection.purchase_count62() == 1 &&
              selection.sprites().pointers[3] == 0 && selection.carried_slot60() == 0,
          "detector purchase debits $400, records ownership and removes its sprite");
    check(selection.characters().charset != charset_before,
          "successful purchase imprints the carried sprite into the vehicle charset");

    clock.tick(selection, {0xFF, '2'});
    finish_script(selection, clock);
    clock.tick(selection);
    move_to_row(selection, clock, 1);
    buy_current_row(selection, clock);
    check(selection.balance() == AccountBalanceBytes{0, 0x10, 0} &&
              selection.traps6a() == 1 && selection.traps6b() == 1 &&
              selection.purchase_count62() == 2 && selection.sprites().pointers[4] == 0x38,
          "trap purchase debits $600, increments both counters and remains repeatable");
    clock.tick(selection, {0xFF, 'E'});
    check(selection.stage() == EquipmentSelection::Stage::finished,
          "uppercase E exits once the mandatory trap exists");
}

void test_unaffordable_and_capacity_boundaries(const Payload& payload)
{
    CharacterFrame previous;
    EquipmentSelection poor(payload, previous, 0, {0, 0x03, 0x99}, 0, 0);
    TestClock poor_clock;
    finish_script(poor, poor_clock);
    poor_clock.tick(poor);
    buy_current_row(poor, poor_clock);
    check(poor.balance() == AccountBalanceBytes{0, 0x03, 0x99} &&
              poor.purchase_count62() == 0 && poor.owned_mask() == 0 &&
              poor.carried_slot60() == 3,
          "unaffordable purchase leaves money, ownership and carried item unchanged");

    constexpr std::array<std::uint8_t, 4> capacities{5, 9, 11, 7};
    constexpr std::array<std::uint8_t, 4> remaining_money{0x70, 0x46, 0x34, 0x58};
    const std::array<std::uint8_t, 32> expected_notice{
        25, 15, 21, 18, 0, 3, 1, 18, 0, 9, 19, 0, 12, 15, 1, 4,
        5, 4, 0, 20, 15, 0, 3, 1, 16, 1, 3, 9, 20, 25, 46, 0,
    };
    for (std::uint8_t vehicle = 0; vehicle < capacities.size(); ++vehicle) {
        EquipmentSelection full(payload, previous, vehicle, {1, 0, 0}, 0, 1);
        TestClock full_clock;
        finish_script(full, full_clock);
        full_clock.tick(full);
        for (unsigned purchase = 0; purchase < capacities[vehicle]; ++purchase) {
            move_to_row(full, full_clock, 1);
            buy_current_row(full, full_clock);
            check(full.purchase_count62() == purchase + 1 && full.traps6a() == purchase + 1,
                  "every purchase through the vehicle capacity is accepted");
        }
        const AccountBalanceBytes balance{0, remaining_money[vehicle], 0};
        check(full.balance() == balance && full.owned_mask() == 0,
              "all four capacities have the correct cumulative trap price");
        move_to_row(full, full_clock, 1);
        press_fire(full, full_clock);
        settle_after_tick(full, full_clock);
        settle_after_tick(full, full_clock, {0xF7, 0});
        press_fire(full, full_clock);
        check(full.purchase_count62() == capacities[vehicle] && full.traps6a() == capacities[vehicle] &&
                  full.traps6b() == capacities[vehicle] && full.carried_slot60() == 4 &&
                  full.balance() == balance && !full.pending_notice().empty(),
              "each full vehicle rejects the next trap without money or inventory changes");
        const auto notice = full.take_pending_notice();
        check(notice.size() == expected_notice.size() &&
                  std::equal(notice.begin(), notice.end(), expected_notice.begin()) &&
                  full.pending_notice().empty(),
              "capacity notice can be handed to the shared scrolling-message layer");
    }
}

void test_empty_owned_slots_do_not_purchase(const Payload& payload)
{
    CharacterFrame previous;
    // All three monitoring items are already owned, so every selectable row
    // is empty.  State 15 can still briefly enter its outward phase after a
    // right input; the following update must normalize that phase before a
    // fire edge can reach the purchase path.
    EquipmentSelection empty(payload, previous, 0, {1, 0, 0}, 0x07, 0);
    TestClock clock;
    finish_script(empty, clock);
    clock.tick(empty);
    check(empty.sprites().pointers[3] == 0 &&
              empty.sprites().pointers[4] == 0 &&
              empty.sprites().pointers[5] == 0,
          "owned monitoring rows start empty");

    clock.tick(empty, {0xF7, 0});
    check((empty.state5f() & 3U) == 1 && empty.carried_slot60() == 0,
          "right may briefly select the outward phase with no carried item");
    clock.tick(empty, {0xEF, 0});
    clock.tick(empty);
    clock.tick(empty, {0xE7, 0});
    clock.tick(empty);

    check(empty.carried_slot60() == 0 && empty.purchase_count62() == 0 &&
              empty.balance() == AccountBalanceBytes{1, 0, 0} &&
              empty.owned_mask() == 0x07,
          "fire on empty owned rows never reaches item price or ownership lookup");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        test_original_tables(payload);
        test_categories_and_graphics(payload);
        testExplicitIrqPhases(payload);
        test_sprite_initialization_and_owned_filter(payload);
        test_rejects_invalid_indices(payload);
        test_state15_navigation_and_category_keys(payload);
        test_purchase_economy_effects_and_exit(payload);
        test_unaffordable_and_capacity_boundaries(payload);
        test_empty_owned_slots_do_not_purchase(payload);
        std::cout << "equipment selection tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
