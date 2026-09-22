#include "assets/embedded.hpp"
#include "assets/payload.hpp"
#include "assets/payload.hpp"
#include "game/pal_counter.hpp"
#include "game/title_screen.hpp"
#include "game/vehicle_selection.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using Payload = ghostbusters::assets::Payload;
using ghostbusters::game::AccountBalanceBytes;
using ghostbusters::game::SoundEvent;
using ghostbusters::game::SoundEvents;
using ghostbusters::game::VehicleSelection;
using ghostbusters::video::CharacterFrame;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct TestClock {
    std::uint8_t current = 0;

    void tick(VehicleSelection& selection)
    {
        current = ghostbusters::game::advance_pal_counter(current);
        selection.tick(current);
    }
};

void tick_until(VehicleSelection& selection, VehicleSelection::Stage expected,
                TestClock& clock)
{
    for (unsigned tick = 0; tick < 4096 && selection.stage() != expected; ++tick) {
        clock.tick(selection);
    }
    check(selection.stage() == expected, "vehicle selection did not reach the expected stage");
}

VehicleSelection makeChoosing(const Payload& payload, const CharacterFrame& previous,
                              AccountBalanceBytes balance, TestClock& clock)
{
    VehicleSelection selection(payload, previous, balance);
    tick_until(selection, VehicleSelection::Stage::choosing, clock);
    return selection;
}

void testInitialMenu(const Payload& payload, const CharacterFrame& previous)
{
    TestClock clock;
    auto selection = makeChoosing(payload, previous, {0x01, 0x00, 0x00}, clock);
    check(selection.row() == 22 && selection.column() == 10,
          "vehicle menu ends at the documented row 22, column 10");
    check(selection.characters().screen[13 * 40 + 10] == 0x24 &&
              selection.characters().colors[13 * 40 + 10] == 1,
          "formatted currency starts at the expected white screen cell");
    check(selection.input_size() == 0 && selection.selected_vehicle() == 0,
          "vehicle menu starts with an empty choice buffer");
}

void testSoundEvents(const Payload& payload, const CharacterFrame& previous)
{
    TestClock clock;
    auto selection = makeChoosing(payload, previous, {0x01, 0x00, 0x00}, clock);
    const auto text_events = selection.take_sound_events();
    check(!text_events.empty() && text_events.front() == SoundEvent::text_tone,
          "vehicle menu forwards text tones from normal-cadence scripts");

    selection.key(0);
    check(selection.take_sound_events().empty(),
          "the no-key sentinel does not click during vehicle input");
    selection.key(0x21);
    check(selection.take_sound_events() == SoundEvents{SoundEvent::key_click} &&
              selection.input_size() == 0,
          "an active but filtered vehicle key clicks before validation");
    selection.key(0x7F);
    check(selection.take_sound_events() == SoundEvents{SoundEvent::key_click} &&
              selection.input_size() == 0,
          "Delete clicks even when the vehicle input is empty");
    selection.key('X');
    check(selection.take_sound_events() == SoundEvents{SoundEvent::key_click} &&
              selection.input_size() == 1,
          "an inserted vehicle key forwards one key click");
    selection.key(0x0D);
    check(selection.take_sound_events() == SoundEvents{SoundEvent::key_click} &&
              selection.stage() == VehicleSelection::Stage::choice_pending && selection.original_state() == 11,
          "Return clicks and defers the invalid-choice dispatcher to the next frame");
    selection.key('X');
    check(selection.take_sound_events().empty(),
          "keys after leaving vehicle input do not click");
}

void testExplicitIrqPhases(const Payload& payload, const CharacterFrame& previous)
{
    const auto scripts = payload.asset("text/balance_prefix");
    check(scripts[0] == 0x0D && scripts[1] == 0x0D && scripts[2] == 'Y',
          "vehicle phase checkpoint no longer has two CRs before Y at $AD50");

    VehicleSelection selection(payload, previous, {0x01, 0x00, 0x00});
    // PAL $08 reaches these four values; their low two bits are phases 0..3.
    constexpr std::array<std::uint8_t, 4> phases{4, 5, 6, 7};
    for (unsigned tick = 0; tick < 8 &&
         selection.stage() != VehicleSelection::Stage::printing_balance_prefix; ++tick) {
        // The initial menu is the original mask-zero script; its phase is
        // deliberately irrelevant. This positions the test at $AD4E.
        selection.tick(phases[3]);
    }
    check(selection.stage() == VehicleSelection::Stage::printing_balance_prefix,
          "vehicle phase checkpoint did not start the balance-prefix script");
    check(selection.take_sound_events().empty(),
          "vehicle mask-zero menu script emits no text tones");

    for (std::size_t phase = 1; phase < phases.size(); ++phase) {
        const auto before = selection.characters();
        selection.tick(phases[phase]);
        check(selection.characters().screen == before.screen &&
                  selection.characters().colors == before.colors,
              "vehicle must consume no text byte before the first CR at phases 1..3");
        check(selection.take_sound_events().empty(),
              "vehicle must emit no text tone before the first CR at phases 1..3");
    }

    selection.tick(phases[0]);
    check(selection.take_sound_events().empty(),
          "vehicle CR at $AD4E does not emit a text tone");
    const auto after_first_cr = selection.characters();
    for (std::size_t phase = 1; phase < phases.size(); ++phase) {
        selection.tick(phases[phase]);
        check(selection.characters().screen == after_first_cr.screen &&
                  selection.characters().colors == after_first_cr.colors,
              "vehicle must consume no text byte after the first CR at phases 1..3");
        check(selection.take_sound_events().empty(),
              "vehicle must emit no text tone after the first CR at phases 1..3");
    }

    selection.tick(phases[0]);
    check(selection.take_sound_events().empty(),
          "vehicle CR at $AD4F does not emit a text tone");
    const auto before_y = selection.characters();
    const auto y_row = selection.row();
    const auto y_column = selection.column();
    check(before_y.screen[static_cast<std::size_t>(y_row) * 40 + y_column] == 0,
          "vehicle Y cell must be empty after the two leading CRs");

    for (std::size_t phase = 1; phase < phases.size(); ++phase) {
        selection.tick(phases[phase]);
        check(selection.characters().screen == before_y.screen &&
                  selection.characters().colors == before_y.colors,
              "vehicle must consume no Y byte at phases 1..3");
        check(selection.take_sound_events().empty(),
              "vehicle must emit no Y text tone at phases 1..3");
    }

    selection.tick(phases[0]);
    check(selection.characters().screen[static_cast<std::size_t>(y_row) * 40 + y_column] == 25,
          "vehicle emits the $AD50 Y only at IRQ phase 0");
    check(selection.take_sound_events() == SoundEvents{SoundEvent::text_tone},
          "vehicle emits the text tone with the phase-0 Y byte");
}

void testInputCapacityAndDelete(const Payload& payload, const CharacterFrame& previous)
{
    TestClock clock;
    auto selection = makeChoosing(payload, previous, {0x09, 0x99, 0x99}, clock);
    for (unsigned index = 0; index < 27; ++index) selection.key('X');
    check(selection.input_size() == 27 && selection.column() == 37,
          "vehicle input accepts 27 bytes through column 36");
    selection.key('X');
    check(selection.input_size() == 27 && selection.column() == 37,
          "vehicle input ignores bytes beyond column 36");
    selection.key(0x7F);
    check(selection.input_size() == 26 && selection.column() == 36,
          "vehicle delete removes one byte and moves the cursor back");
    selection.key('X');
    check(selection.input_size() == 27 && selection.column() == 37,
          "vehicle input can reinsert after delete");
}

void testFirstByteAndPreview(const Payload& payload, const CharacterFrame& previous)
{
    TestClock compact_clock;
    auto compact = makeChoosing(payload, previous, {0x01, 0x00, 0x00}, compact_clock);
    compact.key('1');
    compact.key('X');
    check(compact.stage() == VehicleSelection::Stage::choosing,
          "vehicle choice waits for Return");
    compact.key(0x0D);
    check(compact.original_state() == 11 && compact.balance() == AccountBalanceBytes{1, 0, 0},
          "Return frame neither purchases nor clears the menu");
    compact_clock.tick(compact);
    check(compact.original_state() == 12 && compact.stage() == VehicleSelection::Stage::preparing_charset,
          "next handler purchases and clears, then waits for State12");
    compact_clock.tick(compact);
    check(compact.original_state() == 13, "charset preparation occupies its own frame");
    compact_clock.tick(compact);
    check(compact.stage() == VehicleSelection::Stage::purchased &&
              compact.selected_vehicle() == 0,
          "1X followed by Return purchases the compact vehicle from its first byte");
    check(compact.balance() == AccountBalanceBytes{0x00, 0x80, 0x00},
          "compact purchase subtracts its BCD price");

    TestClock preview_clock;
    auto preview = makeChoosing(payload, previous, {0x01, 0x00, 0x00}, preview_clock);
    preview.key(0x20);
    preview.key(0x0D);
    check(preview.stage() == VehicleSelection::Stage::preview_pending,
          "Space followed by Return waits for the preview update");
    preview_clock.tick(preview);
    check(preview.stage() == VehicleSelection::Stage::printing_preview &&
              preview.selected_vehicle() == 1,
          "the first preview prepares vehicle zero and advances the preview index");
    tick_until(preview, VehicleSelection::Stage::choosing, preview_clock);
    check(preview.row() == 4 && preview.column() == 1,
          "the completed preview details script leaves the input cursor at row 4, column 1");
}

std::uint8_t screen_code(std::uint8_t value)
{
    return value == 0x20 ? 0 : value >= 0x40 ? static_cast<std::uint8_t>(value - 0x40) : value;
}

void start_preview(VehicleSelection& selection, TestClock& clock)
{
    check(selection.stage() == VehicleSelection::Stage::choosing,
          "preview must start from the vehicle choice checkpoint");
    selection.key(0x20);
    selection.key(0x0D);
    check(selection.stage() == VehicleSelection::Stage::preview_pending,
          "Space and Return enter the pending preview stage");
    clock.tick(selection);
    check(selection.stage() == VehicleSelection::Stage::printing_preview,
          "the next tick starts preview graphics and its details script");
    tick_until(selection, VehicleSelection::Stage::choosing, clock);
}

void testPreviewCycleAndGraphics(const Payload& payload, const CharacterFrame& previous)
{
    constexpr std::array<std::uint8_t, 12> grid_codes{64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
    TestClock clock;
    auto selection = makeChoosing(payload, previous, {0x01, 0x00, 0x00}, clock);
    const std::array<std::uint8_t, 4> expected_multicolor2{3, 6, 6, 4};
    for (unsigned shown = 0; shown < 4; ++shown) {
        start_preview(selection, clock);
        check(selection.selected_vehicle() == static_cast<std::uint8_t>((shown + 1) & 3),
              "preview selection index advances cyclically");
        check(selection.row() == 4 && selection.column() == 1,
              "each details script ends at the preview input cursor");
        check(selection.characters().screen[40 + 1] == static_cast<std::uint8_t>('1' + shown),
              "the details script corresponds to the vehicle just previewed");
        check(selection.characters().multicolor && selection.characters().background == 8 &&
                  selection.characters().multicolor1 == 1 &&
                  selection.characters().multicolor2 == expected_multicolor2[shown],
              "preview selects the original multicolor registers");

        for (unsigned row = 0; row < 16; ++row) {
            for (unsigned column = 0; column < 12; ++column) {
                const auto cell = (4 + row) * 40 + 18 + column;
                const auto expected = static_cast<std::uint8_t>(
                    grid_codes[column] + row);
                check(selection.characters().screen[cell] == expected &&
                          selection.characters().colors[cell] == 8,
                      "preview viewport covers rows 4..19, columns 18..29 in color 8");
            }
        }
        for (unsigned row = 0; row < 2; ++row) {
            for (unsigned column = 0; column < 36; ++column) {
                const auto hint = payload.asset(row ? "text/vehicle_hint_1" : "text/vehicle_hint_0");
                const auto cell = (21 + row) * 40 + 1 + column;
                check(selection.characters().screen[cell] == screen_code(hint[column]) &&
                          selection.characters().colors[cell] == 0,
                      "preview caption occupies columns 1..36 on rows 21 and 22");
            }
        }
    }

    // The rotating preview index is only a display choice. A subsequent
    // purchase still uses the first byte typed by the player.
    selection.key('1');
    selection.key(0x0D);
    tick_until(selection, VehicleSelection::Stage::purchased, clock);
    check(selection.stage() == VehicleSelection::Stage::purchased &&
              selection.selected_vehicle() == 0 &&
              selection.balance() == AccountBalanceBytes{0x00, 0x80, 0x00},
          "after the fourth preview, input 1 purchases the compact vehicle");
}

void testPreviewInputCapacity(const Payload& payload, const CharacterFrame& previous)
{
    TestClock clock;
    auto selection = makeChoosing(payload, previous, {0x01, 0x00, 0x00}, clock);
    start_preview(selection, clock);
    check(selection.row() == 4 && selection.column() == 1,
          "preview input begins at row 4, column 1");
    for (unsigned index = 0; index < 36; ++index) selection.key('X');
    check(selection.input_size() == 36 && selection.column() == 37,
          "preview input accepts 36 bytes through column 36");
    selection.key('X');
    check(selection.input_size() == 36 && selection.column() == 37,
          "preview input ignores bytes beyond column 36");
}

void testExactPrices(const Payload& payload, const CharacterFrame& previous)
{
    const std::array<AccountBalanceBytes, 4> prices{
        AccountBalanceBytes{0x00, 0x20, 0x00},
        AccountBalanceBytes{0x00, 0x48, 0x00},
        AccountBalanceBytes{0x00, 0x60, 0x00},
        AccountBalanceBytes{0x01, 0x50, 0x00},
    };
    for (std::size_t index = 0; index < prices.size(); ++index) {
        TestClock clock;
        auto selection = makeChoosing(payload, previous, prices[index], clock);
        selection.key(static_cast<std::uint8_t>('1' + index));
        selection.key(0x0D);
        tick_until(selection, VehicleSelection::Stage::purchased, clock);
        check(selection.stage() == VehicleSelection::Stage::purchased,
              "an exact BCD vehicle price must be accepted");
        check(selection.selected_vehicle() == index,
              "the first input byte selects the corresponding vehicle");
        check(selection.balance() == AccountBalanceBytes{},
              "an exact purchase leaves a zero BCD balance");
    }
}

void testInsufficientBalanceReprintsMenu(const Payload& payload,
                                         const CharacterFrame& previous)
{
    const AccountBalanceBytes balance{0x00, 0x47, 0x00};
    TestClock clock;
    auto selection = makeChoosing(payload, previous, balance, clock);
    selection.key('2');
    selection.key(0x0D);
    clock.tick(selection);
    check(selection.stage() == VehicleSelection::Stage::clearing_menu && selection.original_state() == 6,
          "an unaffordable vehicle dispatches back to State6 before clearing the menu");
    tick_until(selection, VehicleSelection::Stage::choosing, clock);
    check(selection.balance() == balance && selection.input_size() == 0 &&
              selection.selected_vehicle() == 0,
          "an unaffordable purchase preserves balance and resets input");
    check(selection.row() == 22 && selection.column() == 10,
          "the restarted menu restores its documented cursor");
}

} // namespace

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        const auto font = ghostbusters::assets::embedded_font();
        const auto title = ghostbusters::game::prepare_title_screen(payload, font);

        CharacterFrame previous = title.characters;
        previous.charset.fill(0x5A);
        testInitialMenu(payload, previous);
        testSoundEvents(payload, previous);
        testExplicitIrqPhases(payload, previous);
        testInputCapacityAndDelete(payload, previous);
        testFirstByteAndPreview(payload, previous);
        testPreviewCycleAndGraphics(payload, previous);
        testPreviewInputCapacity(payload, previous);
        testExactPrices(payload, previous);
        testInsufficientBalanceReprintsMenu(payload, previous);
        std::cout << "vehicle selection tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
