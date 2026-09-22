#include "assets/embedded.hpp"
#include "assets/payload.hpp"
#include "game/franchise_dialog.hpp"
#include "game/pal_counter.hpp"
#include "game/title_screen.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

using ghostbusters::game::FranchiseDialog;
using ghostbusters::game::StartKey;
using ghostbusters::game::SoundEvent;
using ghostbusters::game::SoundEvents;

struct TestClock {
    std::uint8_t current = 0;

    void tick(FranchiseDialog& dialog)
    {
        current = ghostbusters::game::advance_pal_counter(current);
        dialog.tick(current);
    }
};

void tick_until(FranchiseDialog& dialog, FranchiseDialog::Stage expected,
                TestClock& clock)
{
    for (unsigned tick = 0; tick < 4096 && dialog.stage() != expected; ++tick) {
        clock.tick(dialog);
    }
    check(dialog.stage() == expected, "dialogue did not reach expected stage");
}

FranchiseDialog make_account_dialog(const ghostbusters::assets::Payload& payload,
                                     const ghostbusters::video::CharacterFrame& title,
                                     TestClock& clock,
                                     ghostbusters::game::FranchiseSession* session = nullptr)
{
    FranchiseDialog dialog(payload, title, StartKey::f1, session);
    tick_until(dialog, FranchiseDialog::Stage::entering_name, clock);
    for (const auto value : std::string("EGX")) dialog.key(static_cast<std::uint8_t>(value));
    dialog.key(0x7F);
    dialog.key('O');
    dialog.key('N');
    check(dialog.name().size() == 4, "name editing failed");
    const auto& frame = dialog.characters();
    check(frame.screen[12 * 40 + 19] == 5 && frame.screen[12 * 40 + 22] == 14,
          "name echo is not at the original prompt cursor");
    check(frame.colors[12 * 40 + 19] == 1, "typed name must be white");
    dialog.key(0x0D);
    tick_until(dialog, FranchiseDialog::Stage::account_question, clock);
    return dialog;
}

std::array<std::uint8_t, 20> saved_name(const std::string& text)
{
    std::array<std::uint8_t, 20> name{};
    std::copy(text.begin(), text.end(), name.begin());
    return name;
}

void testFranchiseSession(const ghostbusters::assets::Payload& payload,
                          const ghostbusters::video::CharacterFrame& title)
{
    using ghostbusters::game::AccountBalanceBytes;
    using ghostbusters::game::FranchiseSession;
    using ghostbusters::game::PackedAccount;

    auto ab = saved_name("AB");
    ab[5] = 0x33; // Saved tail bytes remain part of the account checksum.
    FranchiseSession earned{true, ab,
                            ghostbusters::game::encodeAccount(ab, {0x01, 0x56, 0})};
    FranchiseDialog resumed(payload, title, StartKey::f3, &earned);
    check(resumed.stage() == FranchiseDialog::Stage::account_accepted &&
              resumed.original_state() == 6 && !resumed.script_active(),
          "completed F3 session did not skip directly to State 6");
    check(resumed.balance() == AccountBalanceBytes{0x01, 0x56, 0},
          "completed F3 session did not restore the saved $15600 account");
    check(resumed.name().bytes() == ab && resumed.name().size() == 2 &&
              resumed.name().submitted(),
          "completed F3 session did not restore the full saved name buffer");

    FranchiseDialog persisted_f1(payload, title, StartKey::f1, &earned);
    check(persisted_f1.stage() == FranchiseDialog::Stage::printing_name &&
              persisted_f1.script_active(),
          "F1 incorrectly resumed a completed franchise session");

    FranchiseSession first_session;
    FranchiseDialog first_f3(payload, title, StartKey::f3, &first_session);
    check(first_f3.stage() == FranchiseDialog::Stage::printing_name &&
              first_f3.script_active(),
          "first-session F3 incorrectly skipped the franchise dialogue");

    auto invalid_packed = earned.packed_account;
    invalid_packed[0] ^= 0x01;
    FranchiseSession invalid{true, ab, invalid_packed};
    FranchiseDialog invalid_resume(payload, title, StartKey::f3, &invalid);
    check(invalid_resume.stage() == FranchiseDialog::Stage::account_accepted &&
              invalid_resume.balance() == AccountBalanceBytes{1, 0, 0},
          "invalid saved F3 account did not use the original $10000 fallback");

    FranchiseSession zero{true, ab,
                          ghostbusters::game::encodeAccount(ab, {0, 0, 0})};
    FranchiseDialog zero_resume(payload, title, StartKey::f3, &zero);
    check(zero_resume.stage() == FranchiseDialog::Stage::account_accepted &&
              zero_resume.balance() == AccountBalanceBytes{1, 0, 0},
          "zero saved F3 account did not use the original $10000 fallback");

    FranchiseSession partial;
    {
        FranchiseDialog entry(payload, title, StartKey::f1, &partial);
        TestClock clock;
        tick_until(entry, FranchiseDialog::Stage::entering_name, clock);
        entry.key('A');
        entry.key('B');
    }
    check(!partial.completed,
          "a partially entered name incorrectly completed the franchise session");
    FranchiseDialog partial_f3(payload, title, StartKey::f3, &partial);
    check(partial_f3.stage() == FranchiseDialog::Stage::printing_name,
          "F3 resumed a partial, unsubmitted name");

    const PackedAccount prior{0x12, 0x34, 0x56, 0x70};
    FranchiseSession new_franchise{true, saved_name("OLD"), prior};
    TestClock n_clock;
    auto n_dialog = make_account_dialog(payload, title, n_clock, &new_franchise);
    check(new_franchise.completed && new_franchise.name == n_dialog.name().bytes(),
          "submitted name was not copied into the franchise session");
    n_dialog.key('N');
    n_dialog.key(0x0D);
    n_clock.tick(n_dialog);
    check(new_franchise.packed_account == prior,
          "new-franchise N branch overwrote the prior packed account");

    FranchiseSession typed;
    TestClock y_clock;
    auto y_dialog = make_account_dialog(payload, title, y_clock, &typed);
    y_dialog.key('Y');
    y_dialog.key(0x0D);
    tick_until(y_dialog, FranchiseDialog::Stage::entering_account_number, y_clock);
    const auto packed = ghostbusters::game::encodeAccount(typed.name, {1, 0, 0});
    for (const auto byte : packed) {
        y_dialog.key(static_cast<std::uint8_t>('0' + (byte >> 4)));
        y_dialog.key(static_cast<std::uint8_t>('0' + (byte & 15)));
    }
    y_dialog.key(0x0D);
    y_clock.tick(y_dialog);
    check(y_dialog.stage() == FranchiseDialog::Stage::account_accepted &&
              typed.packed_account == packed,
          "typed account was not retained in the franchise session");

    FranchiseSession rejected{false, {}, prior};
    TestClock rejected_clock;
    auto rejected_dialog = make_account_dialog(payload, title, rejected_clock, &rejected);
    rejected_dialog.key('Y');
    rejected_dialog.key(0x0D);
    tick_until(rejected_dialog, FranchiseDialog::Stage::entering_account_number,
               rejected_clock);
    rejected_dialog.key(0x0D);
    rejected_clock.tick(rejected_dialog);
    check(rejected_dialog.stage() == FranchiseDialog::Stage::printing_invalid_account &&
              rejected.packed_account == PackedAccount{},
          "rejected typed account was not retained before validation");
}

void testSoundEvents(const ghostbusters::assets::Payload& payload,
                     const ghostbusters::video::CharacterFrame& title)
{
    FranchiseDialog dialog(payload, title, StartKey::f1);
    TestClock clock;
    dialog.key('A');
    check(dialog.take_sound_events().empty(),
          "keys while a dialogue script is printing do not click");
    tick_until(dialog, FranchiseDialog::Stage::entering_name, clock);
    const auto text_events = dialog.take_sound_events();
    check(!text_events.empty() && text_events.front() == SoundEvent::text_tone,
          "dialogue forwards text tones from its script");

    dialog.key(0);
    check(dialog.take_sound_events().empty(),
          "the no-key sentinel does not trigger an input click");
    dialog.key(0x21);
    const auto filtered_events = dialog.take_sound_events();
    check(filtered_events.size() == 1 && filtered_events[0] == SoundEvent::key_click &&
              dialog.name().size() == 0,
          "an active but filtered input still clicks before validation");
    dialog.key('A');
    check(dialog.take_sound_events() == SoundEvents{SoundEvent::key_click} &&
              dialog.name().size() == 1,
          "an inserted name byte forwards one key click");
    dialog.key(0x7F);
    check(dialog.take_sound_events() == SoundEvents{SoundEvent::key_click} &&
              dialog.name().size() == 0,
          "Delete forwards one key click on the active input path");
    dialog.key(0x0D);
    check(dialog.take_sound_events() == SoundEvents{SoundEvent::key_click} &&
              dialog.original_state() == 1 && !dialog.script_active(),
          "Return clicks and ends input without dispatching State1 in the same frame");
    dialog.key('B');
    check(dialog.take_sound_events().empty(),
          "keys after Return and before the next dispatch do not click");
    clock.tick(dialog);
    check(dialog.original_state() == 2 && dialog.script_active(),
          "the following frame dispatches State1 and starts the account script");
}

void testExplicitIrqPhases(const ghostbusters::assets::Payload& payload,
                           const ghostbusters::video::CharacterFrame& title)
{
    const auto scripts = payload.asset("text/franchise_intro");
    check(scripts[0] == 0x20 && scripts[1] == 0x20 &&
              scripts[2] == 0x20 && scripts[3] == 0x20 && scripts[4] == 'G',
          "dialogue phase checkpoint no longer has four spaces before G at $AB17");

    FranchiseDialog dialog(payload, title, StartKey::f1);
    // PAL $08 reaches these four values; their low two bits are phases 0..3.
    constexpr std::array<std::uint8_t, 4> phases{4, 5, 6, 7};
    for (unsigned space = 0; space < 4; ++space) {
        dialog.tick(phases[0]);
        check(dialog.take_sound_events().empty(),
              "dialogue spaces do not emit a text tone at phase 0");
        check(dialog.stage() == FranchiseDialog::Stage::printing_name,
              "dialogue remains in the initial script after a space");

        for (std::size_t phase = 1; phase < phases.size(); ++phase) {
            const auto before = dialog.characters();
            dialog.tick(phases[phase]);
            check(dialog.characters().screen == before.screen &&
                      dialog.characters().colors == before.colors,
                  "dialogue must consume no text byte at IRQ phases 1..3");
            check(dialog.take_sound_events().empty(),
                  "dialogue must emit no text tone at IRQ phases 1..3");
        }
    }

    check(dialog.characters().screen[40 + 12] == 0,
          "dialogue must leave G pending after the four leading spaces");
    dialog.tick(phases[0]);
    check(dialog.characters().screen[40 + 12] == 7,
          "dialogue emits the $AB17 G only at IRQ phase 0");
    check(dialog.take_sound_events() == SoundEvents{SoundEvent::text_tone},
          "dialogue emits the text tone with the phase-0 G byte");

    for (std::size_t phase = 1; phase < phases.size(); ++phase) {
        const auto before = dialog.characters();
        dialog.tick(phases[phase]);
        check(dialog.characters().screen == before.screen &&
                  dialog.characters().colors == before.colors,
              "dialogue must not emit another text byte at phases 1..3");
        check(dialog.take_sound_events().empty(),
              "dialogue must not emit another text tone at phases 1..3");
    }
}
}

int main()
{
    try {
        const auto payload = ghostbusters::assets::Payload::embedded();
        const auto new_account = payload.asset("text/new_account");
        const auto account_number = payload.asset("text/account_number");
        check(new_account.front() == 0x0D && new_account.back() == 0xFF,
              "new-account script boundaries do not match the pinned payload");
        check(account_number.front() == 0x0D && account_number.back() == 0xFF,
              "account-number script boundaries do not match the pinned payload");
        const auto font = ghostbusters::assets::embedded_font();
        const auto title = ghostbusters::game::prepare_title_screen(payload, font);
        testSoundEvents(payload, title.characters);
        testExplicitIrqPhases(payload, title.characters);
        testFranchiseSession(payload, title.characters);
        using ghostbusters::game::FranchiseDialog;
        using ghostbusters::game::StartKey;
        for (const auto key : {StartKey::f1, StartKey::f3}) {
            FranchiseDialog dialog(payload, title.characters, key);
            TestClock clock;
            dialog.key('X');
            check(dialog.name().size() == 0, "Typing while script runs should be ignored");
            tick_until(dialog, FranchiseDialog::Stage::entering_name, clock);
            check(dialog.start_key() == key, "Original F1/F3 identity was discarded");
            dialog.key('E'); dialog.key('G'); dialog.key('X'); dialog.key(0x7F); dialog.key('O'); dialog.key('N');
            check(dialog.name().size() == 4, "Name editing failed");
            const auto& frame = dialog.characters();
            check(frame.screen[12 * 40 + 19] == 5 && frame.screen[12 * 40 + 22] == 14,
                  "Name echo is not at the original prompt cursor");
            check(frame.colors[12 * 40 + 19] == 1, "Typed name must be white");
            dialog.key(0x0D);
            tick_until(dialog, FranchiseDialog::Stage::account_question, clock);
            check(dialog.name().bytes()[4] == 0, "Submitted name is not terminated");
            check(dialog.answer().size() == 0, "Account response must start empty");
        }

        TestClock y_clock;
        auto y_dialog = make_account_dialog(payload, title.characters, y_clock);
        const auto saved_name = y_dialog.name().bytes();
        y_dialog.key('Y');
        check(y_dialog.characters().screen[13 * 40 + 25] == 0x19 &&
                  y_dialog.characters().colors[13 * 40 + 25] == 1,
              "Account response must echo in white at the question cursor");
        y_dialog.key(0x7F);
        check(y_dialog.characters().screen[13 * 40 + 25] == 0 &&
                  y_dialog.characters().colors[13 * 40 + 25] == 1,
              "Account delete must erase with a white space");
        y_dialog.key('Y');
        check(y_dialog.stage() == FranchiseDialog::Stage::account_question,
              "Y must wait for Return before choosing a branch");
        y_dialog.key(0x0D);
        y_clock.tick(y_dialog);
        check(y_dialog.stage() == FranchiseDialog::Stage::printing_account_number,
              "Y Return did not start the account-number script");
        tick_until(y_dialog, FranchiseDialog::Stage::entering_account_number, y_clock);
        check(y_dialog.characters().screen[14 * 40 + 1] == 0x17 &&
                  y_dialog.characters().colors[14 * 40 + 1] == 0,
              "Account-number prompt did not render from its pinned script");
        check(y_dialog.name().bytes() == saved_name, "Y branch changed the saved name");

        y_dialog.key(0x0D); // Empty account packs as zero and must fail.
        y_clock.tick(y_dialog);
        check(y_dialog.stage() == FranchiseDialog::Stage::printing_invalid_account,
              "Empty account was not rejected");
        tick_until(y_dialog, FranchiseDialog::Stage::waiting_invalid_account, y_clock);
        check(y_dialog.characters().screen[15 * 40 + 1] == 9,
              "Invalid account message is missing");
        tick_until(y_dialog, FranchiseDialog::Stage::account_question, y_clock);
        check(y_dialog.name().bytes() == saved_name, "Invalid account retry changed name");
        y_dialog.key('Y'); y_dialog.key(0x0D);
        tick_until(y_dialog, FranchiseDialog::Stage::entering_account_number, y_clock);
        const auto packed = ghostbusters::game::encodeAccount(saved_name, {1, 0, 0});
        for (const auto byte : packed) {
            y_dialog.key(static_cast<std::uint8_t>('0' + (byte >> 4)));
            y_dialog.key(static_cast<std::uint8_t>('0' + (byte & 15)));
        }
        y_dialog.key('9');
        check(y_dialog.account_number().size() == 8, "Account input exceeds eight characters");
        check(y_dialog.stage() == FranchiseDialog::Stage::entering_account_number,
              "Account input must wait for Return");
        y_dialog.key(0x0D);
        y_clock.tick(y_dialog);
        check(y_dialog.stage() == FranchiseDialog::Stage::account_accepted &&
                  y_dialog.balance() == ghostbusters::game::AccountBalanceBytes{1, 0, 0},
              "Valid account did not restore original balance bytes");

        TestClock n_clock;
        auto n_dialog = make_account_dialog(payload, title.characters, n_clock);
        n_dialog.key('N');
        n_dialog.key(0x0D);
        n_clock.tick(n_dialog);
        check(n_dialog.stage() == FranchiseDialog::Stage::printing_new_account_notice,
              "N Return did not start the new-account script");
        tick_until(n_dialog, FranchiseDialog::Stage::new_account_notice, n_clock);
        check(n_dialog.characters().screen[15 * 40 + 1] == 9 &&
                  n_dialog.characters().colors[15 * 40 + 1] == 0,
              "New-account notice did not render from its pinned script");
        check(n_dialog.balance() == ghostbusters::game::AccountBalanceBytes{1, 0, 0},
              "New franchise did not receive the original 010000 balance");
        check(n_dialog.name().bytes() == saved_name, "N branch changed the saved name");

        TestClock invalid_clock;
        auto invalid_dialog = make_account_dialog(payload, title.characters, invalid_clock);
        invalid_dialog.key('Q');
        check(invalid_dialog.stage() == FranchiseDialog::Stage::account_question,
              "Invalid response must wait for Return");
        invalid_dialog.key(0x0D);
        invalid_clock.tick(invalid_dialog);
        check(invalid_dialog.stage() == FranchiseDialog::Stage::clearing_account_answer,
              "Invalid response did not enter the reference erase path");
        // Runtime $EB00 consists of 120 spaces and clears from row 13,col 1
        // linearly. It must not erase the saved name on the preceding rows.
        for (unsigned tick = 0; tick < 3; ++tick) invalid_clock.tick(invalid_dialog);
        check(invalid_dialog.stage() == FranchiseDialog::Stage::clearing_account_answer,
              "Runtime erase script must keep its terminator for the fourth frame");
        invalid_clock.tick(invalid_dialog);
        check(invalid_dialog.stage() == FranchiseDialog::Stage::printing_account_question_retry,
              "Invalid response did not finish the runtime erase script");
        tick_until(invalid_dialog, FranchiseDialog::Stage::account_question, invalid_clock);
        check(invalid_dialog.name().bytes() == saved_name, "Retry changed the saved name");
        check(invalid_dialog.answer().size() == 0, "Retry did not reset the account response");
        for (unsigned index = 0; index < 13; ++index) invalid_dialog.key('A');
        check(invalid_dialog.answer().size() == 12,
              "Account response capacity must be min(19, 37-column)");

        std::cout << "F1/F3, account branching, and franchise dialogue tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
