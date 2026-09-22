#include "game/vehicle_selection.hpp"
#include "assets/ui_text_data.hpp"
#include "game/money_text.hpp"
#include "game/bcd_money.hpp"
#include "game/vehicle_graphics.hpp"

#include <algorithm>
#include <utility>

namespace ghostbusters::game {

VehicleSelection::VehicleSelection(const assets::Payload& payload,
                                   const video::CharacterFrame& previous,
                                   AccountBalanceBytes balance)
    : payload_(payload), characters_(previous), balance_(balance)
{
    clear_menu();
}

void VehicleSelection::restart_menu()
{
    stage_ = Stage::clearing_menu;
    state3a_ = 6;
}

void VehicleSelection::clear_menu()
{
    std::fill_n(characters_.screen.begin(), 0x3C0, 0);
    std::fill_n(characters_.colors.begin(), 0x3C0, 0);
    characters_.background = 8;
    script_.set_cursor(1, 1);
    stage_ = Stage::menu_pending;
    state3a_ = 7;
    clears_input_ = true;
    input_size_ = 0;
    input_.fill(0);
}

void VehicleSelection::tick(const std::uint8_t irq_counter, std::uint8_t translated_key)
{
    rendered_bytes_.clear();
    clears_input_ = false;
    if (stage_ == Stage::clearing_menu) { clear_menu(); return; }
    if (stage_ == Stage::menu_pending) {
        script_.start(assets::UiTextData(payload_).script(
                          assets::UiScript::vehicle_menu),
                      1, 1, 0);
        stage_ = Stage::printing_menu;
        state3a_ = 8;
        clears_input_ = true;
        return;
    }
    if (stage_ == Stage::choice_pending) { finish_choice(); return; }
    if (stage_ == Stage::preparing_charset) {
        prepare_vehicle_charset(payload_, characters_, selected_vehicle_);
        stage_ = Stage::drawing_vehicle;
        state3a_ = 13;
        clears_input_ = true;
        return;
    }
    if (stage_ == Stage::drawing_vehicle) {
        draw_vehicle_grid(payload_, characters_);
        stage_ = Stage::purchased;
        state3a_ = 14;
        clears_input_ = true;
        return;
    }
    if (stage_ == Stage::preview_pending) {
        begin_preview();
        return;
    }
    if (!script_.active()) { key(translated_key); return; }
    script_.tick(characters_, irq_counter,
                 stage_ == Stage::printing_menu ? 0 : 3);
    rendered_bytes_ = script_.rendered_bytes();
    auto events = script_.take_sound_events();
    sound_events_.insert(sound_events_.end(), events.begin(), events.end());
    if (script_.active()) return;
    const auto column = script_.column();
    const auto row = script_.row();
    switch (stage_) {
    case Stage::printing_menu:
        script_.start(assets::UiTextData(payload_).script(
                          assets::UiScript::balance_prefix),
                      column, row, 0);
        stage_ = Stage::printing_balance_prefix;
        state3a_ = 9;
        clears_input_ = true;
        break;
    case Stage::printing_balance_prefix:
        script_.start(format_money(balance_), column, row, 1);
        stage_ = Stage::printing_balance;
        state3a_ = 10;
        clears_input_ = true;
        break;
    case Stage::printing_balance:
        selected_vehicle_ = 0; // $75B5 resets the preview/selection index here.
        script_.start(assets::UiTextData(payload_).script(
                          assets::UiScript::vehicle_instructions),
                      column, row, 0);
        stage_ = Stage::printing_instructions;
        state3a_ = 11;
        clears_input_ = true;
        break;
    case Stage::printing_instructions:
    case Stage::printing_preview:
        input_size_ = 0;
        input_.fill(0);
        stage_ = Stage::choosing;
        key(translated_key);
        break;
    case Stage::choosing:
    case Stage::clearing_menu:
    case Stage::menu_pending:
    case Stage::preview_pending:
    case Stage::choice_pending:
    case Stage::preparing_charset:
    case Stage::drawing_vehicle:
    case Stage::purchased:
        break;
    }
}

SoundEvents VehicleSelection::take_sound_events()
{
    auto events = std::move(sound_events_);
    sound_events_.clear();
    return events;
}

void VehicleSelection::begin_preview()
{
    const assets::UiTextData ui(payload_);
    const auto shown = selected_vehicle_;
    characters_.screen.fill(0);
    characters_.colors.fill(0);
    prepare_vehicle_graphics(payload_, characters_, shown);
    for (unsigned row = 0; row < 2; ++row) {
        for (unsigned column = 0; column < 36; ++column) {
            const auto code = ui.vehicle_hint_row(row)[column];
            characters_.screen[(21 + row) * 40 + 1 + column] = code == 0x20 ? 0 :
                code >= 0x40 ? static_cast<std::uint8_t>(code - 0x40) : code;
        }
    }
    selected_vehicle_ = static_cast<std::uint8_t>((shown + 1) & 3);
    script_.start(ui.vehicle_preview(shown), 1, 1, 0);
    stage_ = Stage::printing_preview;
}

void VehicleSelection::key(std::uint8_t translated)
{
    if (stage_ != Stage::choosing || translated == 0) return;
    clears_input_ = true; // Original text-input handler clears every nonzero sample.
    // The original text-input handler clicks before checking Return, Delete,
    // function sentinels, or whether the character fits the cursor bound.
    sound_events_.push_back(SoundEvent::key_click);
    const auto column = script_.column();
    const auto row = script_.row();
    if (translated == 0x0D) {
        input_[input_size_] = 0;
        stage_ = input_[0] == 0x20 ? Stage::preview_pending : Stage::choice_pending;
        return;
    }
    if (translated == 0x7F) {
        if (input_size_ == 0) return;
        rendered_bytes_.push_back(0x20);
        --input_size_;
        input_[input_size_] = 0x20;
        characters_.screen[row * 40 + column - 1] = 0;
        characters_.colors[row * 40 + column - 1] = 1;
        script_.set_cursor(static_cast<std::uint8_t>(column - 1), row);
    } else if (translated >= 0x20 && translated <= 0x7E && translated != 0x21 &&
               translated != 0x2F && input_size_ < 36 && column < 37) {
        rendered_bytes_.push_back(translated);
        input_[input_size_++] = translated;
        characters_.screen[row * 40 + column] = translated == 0x20 ? 0 :
            translated >= 0x40 ? static_cast<std::uint8_t>(translated - 0x40) : translated;
        characters_.colors[row * 40 + column] = 1;
        script_.set_cursor(static_cast<std::uint8_t>(column + 1), row);
    }
}

void VehicleSelection::finish_choice()
{
    const auto first = input_[0];
    if (first < '1' || first > '4') { restart_menu(); return; }
    selected_vehicle_ = static_cast<std::uint8_t>(first - '1');
    const auto named_price =
        assets::UiTextData(payload_).vehicle_price(selected_vehicle_);
    const AccountBalanceBytes price{named_price.high_bcd,
                                    named_price.middle_bcd, 0};
    if (!canAfford(balance_, price)) { restart_menu(); return; }
    balance_ = subtractMoney(balance_, price);
    std::fill_n(characters_.screen.begin(), 0x3C0, 0);
    std::fill_n(characters_.colors.begin(), 0x3C0, 0);
    script_.set_cursor(1, 1);
    stage_ = Stage::preparing_charset;
    state3a_ = 12;
    clears_input_ = true;
}

} // namespace ghostbusters::game
