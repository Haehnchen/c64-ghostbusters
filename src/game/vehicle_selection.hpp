#pragma once

#include "assets/payload.hpp"
#include "game/account_codec.hpp"
#include "game/sound_event.hpp"
#include "game/text_script.hpp"
#include "video/character_frame.hpp"

#include <cstdint>
#include <array>

namespace ghostbusters::game {

class VehicleSelection {
public:
    enum class Stage { clearing_menu, menu_pending, printing_menu, printing_balance_prefix, printing_balance,
                       printing_instructions, choosing, preview_pending, choice_pending, printing_preview,
                       preparing_charset, drawing_vehicle, purchased };

    VehicleSelection(const assets::Payload& payload, const video::CharacterFrame& previous,
                     AccountBalanceBytes balance);
    void tick(std::uint8_t irq_counter, std::uint8_t translated_key = 0);
    void key(std::uint8_t translated);
    // True only when this frame executes an original store clearing $17.
    [[nodiscard]] bool clears_input() const noexcept { return clears_input_; }
    [[nodiscard]] Stage stage() const { return stage_; }
    [[nodiscard]] const video::CharacterFrame& characters() const { return characters_; }
    [[nodiscard]] AccountBalanceBytes balance() const { return balance_; }
    [[nodiscard]] std::uint8_t column() const { return script_.column(); }
    [[nodiscard]] std::uint8_t row() const { return script_.row(); }
    [[nodiscard]] std::uint8_t selected_vehicle() const { return selected_vehicle_; }
    [[nodiscard]] std::uint8_t input_size() const { return input_size_; }
    [[nodiscard]] std::uint8_t original_state() const noexcept { return state3a_; }
    [[nodiscard]] bool script_active() const noexcept { return script_.active(); }
    [[nodiscard]] const std::vector<std::uint8_t>& rendered_bytes() const noexcept { return rendered_bytes_; }
    [[nodiscard]] SoundEvents take_sound_events();

private:
    const assets::Payload& payload_;
    video::CharacterFrame characters_;
    AccountBalanceBytes balance_;
    TextScript script_;
    bool clears_input_ = false;
    Stage stage_ = Stage::printing_menu;
    // The original cursor limit permits 27 characters in the main menu and
    // 36 in a preview, extending into the already displayed runtime text.
    std::array<std::uint8_t, 37> input_{};
    std::uint8_t input_size_ = 0;
    std::uint8_t selected_vehicle_ = 0;
    SoundEvents sound_events_;
    std::vector<std::uint8_t> rendered_bytes_;
    std::uint8_t state3a_ = 7;
    void restart_menu();
    void clear_menu();
    void finish_choice();
    void begin_preview();
};

} // namespace ghostbusters::game
