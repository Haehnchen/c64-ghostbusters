#include "game/franchise_dialog.hpp"
#include "assets/ui_text_data.hpp"
#include "game/account_number.hpp"

#include <algorithm>
#include <utility>

namespace ghostbusters::game {
namespace {

constexpr std::size_t kScreenColumns = 40;
constexpr std::uint8_t kQuestionRow = 13;
constexpr std::uint8_t kQuestionColumn = 1;

std::uint8_t screen_code(std::uint8_t translated)
{
    if (translated == 0x20) return 0;
    if (translated >= 0x40) return static_cast<std::uint8_t>(translated - 0x40);
    return translated;
}

} // namespace

FranchiseDialog::FranchiseDialog(const assets::Payload& payload,
                                 const video::CharacterFrame& title, StartKey key,
                                 FranchiseSession* session)
    : payload_(payload), characters_(title), start_key_(key), session_(session)
{
    std::fill_n(characters_.screen.begin(), 0x3C0, 0);
    std::fill_n(characters_.colors.begin(), 0x3C0, 0);
    // $9004 resets $3B to $08; the IRQ writes it to D021 for the main text area.
    characters_.background = 8;

    // Resume only a completed franchise. Invalid or zero saved accounts
    // use the initial loan instead of reopening the name/account dialogue.
    if (key == StartKey::f3 && session_ != nullptr && session_->completed) {
        name_ = NameEntry(session_->name);
        const auto decoded = decodeAccount(name_.bytes(), session_->packed_account);
        balance_ = decoded.usable ? decoded.balance : AccountBalanceBytes{1, 0, 0};
        stage_ = Stage::account_accepted;
        state3a_ = 6;
        return;
    }

    script_.start(assets::UiTextData(payload_).script(
        assets::UiScript::franchise_intro));
}

void FranchiseDialog::tick(const std::uint8_t irq_counter, std::uint8_t translated_key)
{
    rendered_bytes_.clear();
    clears_input_ = false;
    if (invalid_countdown_ != 0) --invalid_countdown_;
    // Return ends $7347-$7396; the next frame dispatches the waiting handler.
    const auto pending = std::exchange(pending_, Pending::none);
    if (pending != Pending::none) {
        if (pending == Pending::name) {
            if (session_ != nullptr) {
                session_->name = name_.bytes();
                session_->completed = true;
            }
            begin_account_question(script_.column(), script_.row());
        }
        else if (pending == Pending::answer) begin_answer_result();
        else validate_account();
        return;
    }
    if (stage_ == Stage::waiting_invalid_account && invalid_countdown_ == 0) {
        account_erase_index_ = 0;
        stage_ = Stage::clearing_account_answer;
        state3a_ = 5;
        clears_input_ = true; // $7559 -> $8D86.
        return;
    }

    if (stage_ == Stage::clearing_account_answer) {
        if (account_erase_index_ < kAccountEraseLength) {
            // The cadence-zero script consumes 40 bytes in each frame. The
            // generated runtime script contains 120 spaces followed by FF,
            // so the terminator is consumed on the fourth frame.
            for (unsigned count = 0; count < 40 && account_erase_index_ < kAccountEraseLength;
                 ++count) {
                clear_account_answer_cell();
            }
            return;
        }
        // State 5 restores the cursor one row above the question and starts
        // script 2 again. Its leading CR puts the prompt at row 13, col 1.
        script_.start(assets::UiTextData(payload_).script(
                          assets::UiScript::account_question),
                      kQuestionColumn, 12, 0);
        stage_ = Stage::printing_account_question_retry;
        state3a_ = 2;
        clears_input_ = true; // $756F -> $74D5 -> $8D86.
        return;
    }

    if (script_.active()) {
        script_.tick(characters_, irq_counter);
        rendered_bytes_ = script_.rendered_bytes();
        auto events = script_.take_sound_events();
        sound_events_.insert(sound_events_.end(), events.begin(), events.end());
        if (!script_.active()) finish_script();
    }
    // The FF branch falls through to input in the same original frame.
    if (!script_.active()) key(translated_key);
}

SoundEvents FranchiseDialog::take_sound_events()
{
    auto events = std::move(sound_events_);
    sound_events_.clear();
    return events;
}

void FranchiseDialog::finish_script()
{
    switch (stage_) {
    case Stage::printing_name:
        stage_ = Stage::entering_name;
        break;
    case Stage::printing_account_question:
    case Stage::printing_account_question_retry:
        // The original input routine allows up to the lesser of 19 bytes and
        // the remaining columns before column 37. Account question script 2
        // leaves the cursor at column 25, hence its normal 12-byte input.
        answer_ = NameEntry(input_capacity(script_.column()));
        stage_ = Stage::account_question;
        break;
    case Stage::printing_account_number:
        account_number_ = NameEntry(8);
        stage_ = Stage::entering_account_number;
        break;
    case Stage::printing_invalid_account:
        stage_ = Stage::waiting_invalid_account;
        break;
    case Stage::printing_new_account_notice:
        stage_ = Stage::new_account_notice;
        break;
    case Stage::account_question:
    case Stage::entering_name:
    case Stage::clearing_account_answer:
    case Stage::entering_account_number:
    case Stage::new_account_notice:
    case Stage::waiting_invalid_account:
    case Stage::account_accepted:
        break;
    }
}

std::size_t FranchiseDialog::input_capacity(std::uint8_t column)
{
    const auto available = column < 37 ? 37U - column : 0U;
    return std::min<std::size_t>(19, available);
}

NameEntry::Result FranchiseDialog::echo_key(std::uint8_t translated, NameEntry& entry)
{
    const auto result = entry.key(translated);
    const auto row = script_.row();
    const auto column = script_.column();

    if (result == NameEntry::Result::inserted) {
        rendered_bytes_.push_back(translated);
        const auto index = static_cast<std::size_t>(row) * kScreenColumns + column;
        characters_.screen[index] = screen_code(translated);
        characters_.colors[index] = 1;
        script_.set_cursor(static_cast<std::uint8_t>(column + 1), row);
    } else if (result == NameEntry::Result::erased) {
        rendered_bytes_.push_back(0x20);
        const auto index = static_cast<std::size_t>(row) * kScreenColumns + column - 1;
        characters_.screen[index] = 0;
        characters_.colors[index] = 1;
        script_.set_cursor(static_cast<std::uint8_t>(column - 1), row);
    }
    return result;
}

void FranchiseDialog::begin_account_question(std::uint8_t column, std::uint8_t row)
{
    script_.start(assets::UiTextData(payload_).script(
                      assets::UiScript::account_question),
                  column, row, 0);
    stage_ = Stage::printing_account_question;
    state3a_ = 2;
    clears_input_ = true; // $74D5.
}

void FranchiseDialog::begin_answer_result()
{
    const auto first = answer_.bytes()[0];
    const auto column = script_.column();
    const auto row = script_.row();

    if (first == static_cast<std::uint8_t>('Y')) {
        script_.start(assets::UiTextData(payload_).script(
                          assets::UiScript::account_number),
                      column, row, 0);
        stage_ = Stage::printing_account_number;
        state3a_ = 3;
        clears_input_ = true; // $7502.
        return;
    }
    if (first == static_cast<std::uint8_t>('N')) {
        balance_ = {1, 0, 0};
        script_.start(assets::UiTextData(payload_).script(
                          assets::UiScript::new_account),
                      column, row, 0);
        stage_ = Stage::printing_new_account_notice;
        state3a_ = 6;
        return;
    }

    // State 4 runs a generated 120-space script at cadence zero. It starts at
    // row 13, column 1 and deliberately does not wrap at column 31; this is
    // the runtime $EB00 script built during startup. State 5 then reprints
    // the account question and re-enables the response input.
    account_erase_index_ = 0;
    stage_ = Stage::clearing_account_answer;
    state3a_ = 5;
    clears_input_ = true; // Invalid answer falls into $7559.
}

void FranchiseDialog::clear_account_answer_cell()
{
    rendered_bytes_.push_back(0x20);
    const auto screen_index = static_cast<std::size_t>(kQuestionRow) * kScreenColumns
                            + kQuestionColumn + account_erase_index_;
    characters_.screen[screen_index] = 0;
    characters_.colors[screen_index] = 0;
    ++account_erase_index_;
}

void FranchiseDialog::key(std::uint8_t translated)
{
    const auto input_active = stage_ == Stage::entering_name ||
                              stage_ == Stage::account_question ||
                              stage_ == Stage::entering_account_number;
    if (!input_active || pending_ != Pending::none || translated == 0) return;
    clears_input_ = true; // $7392-$7394, including rejected input.
    // $734F runs before Return/Delete handling and before the input byte's
    // validity checks. The two function-key sentinels are filtered only after
    // this click intent has been emitted.
    sound_events_.push_back(SoundEvent::key_click);
    if (translated == 0x21 || translated == 0x2F) return;

    if (stage_ == Stage::entering_name) {
        const auto result = echo_key(translated, name_);
        if (result == NameEntry::Result::submitted) {
            pending_ = Pending::name;
        }
    } else if (stage_ == Stage::account_question) {
        const auto result = echo_key(translated, answer_);
        if (result == NameEntry::Result::submitted) pending_ = Pending::answer;
    } else if (stage_ == Stage::entering_account_number) {
        const auto result = echo_key(translated, account_number_);
        if (result == NameEntry::Result::submitted) {
            pending_ = Pending::account;
        }
    }
}

void FranchiseDialog::validate_account()
{
    const auto packed = pack_account_number(account_number_.bytes());
    if (session_ != nullptr) session_->packed_account = packed;
    const auto decoded = decodeAccount(name_.bytes(), packed);
    balance_ = decoded.balance;
    if (decoded.usable) {
        stage_ = Stage::account_accepted;
        state3a_ = 6;
    } else {
        script_.start(assets::UiTextData(payload_).script(
                          assets::UiScript::invalid_account),
                      1, 15, 0);
        invalid_countdown_ = 0xBF;
        stage_ = Stage::printing_invalid_account;
        state3a_ = 4;
        clears_input_ = true; // $753A.
    }
}

} // namespace ghostbusters::game
