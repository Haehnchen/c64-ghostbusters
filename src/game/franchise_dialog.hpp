#pragma once

#include "assets/payload.hpp"
#include "game/name_entry.hpp"
#include "game/account_codec.hpp"
#include "game/franchise_session.hpp"
#include "game/sound_event.hpp"
#include "game/text_script.hpp"
#include "video/character_frame.hpp"

#include <cstddef>
#include <cstdint>

namespace ghostbusters::game {

enum class StartKey : std::uint8_t { f1 = 0x20, f3 = 0x28 };

class FranchiseDialog {
public:
    enum class Stage {
        printing_name,
        entering_name,
        printing_account_question,
        account_question,
        clearing_account_answer,
        printing_account_question_retry,
        printing_account_number,
        entering_account_number,
        printing_invalid_account,
        waiting_invalid_account,
        account_accepted,
        printing_new_account_notice,
        new_account_notice,
    };

    FranchiseDialog(const assets::Payload& payload, const video::CharacterFrame& title,
                    StartKey key, FranchiseSession* session = nullptr);
    void tick(std::uint8_t irq_counter, std::uint8_t translated_key = 0);
    void key(std::uint8_t translated);
    // True only when this frame executes an original store clearing $17.
    [[nodiscard]] bool clears_input() const noexcept { return clears_input_; }
    [[nodiscard]] Stage stage() const { return stage_; }
    [[nodiscard]] const video::CharacterFrame& characters() const { return characters_; }
    [[nodiscard]] const NameEntry& name() const { return name_; }
    [[nodiscard]] const NameEntry& answer() const { return answer_; }
    [[nodiscard]] const NameEntry& account_number() const { return account_number_; }
    [[nodiscard]] AccountBalanceBytes balance() const { return balance_; }
    [[nodiscard]] StartKey start_key() const { return start_key_; }
    [[nodiscard]] std::uint8_t original_state() const noexcept { return state3a_; }
    [[nodiscard]] bool script_active() const noexcept { return script_.active(); }
    [[nodiscard]] std::uint8_t column() const noexcept { return script_.column(); }
    [[nodiscard]] std::uint8_t row() const noexcept { return script_.row(); }
    [[nodiscard]] const std::vector<std::uint8_t>& rendered_bytes() const noexcept { return rendered_bytes_; }
    [[nodiscard]] SoundEvents take_sound_events();

private:
    static constexpr std::size_t kAccountEraseLength = 120;

    void finish_script();
    [[nodiscard]] NameEntry::Result echo_key(std::uint8_t translated, NameEntry& entry);
    void begin_account_question(std::uint8_t column, std::uint8_t row);
    void begin_answer_result();
    void clear_account_answer_cell();
    void validate_account();
    [[nodiscard]] static std::size_t input_capacity(std::uint8_t column);

    const assets::Payload& payload_;
    video::CharacterFrame characters_;
    TextScript script_;
    NameEntry name_;
    NameEntry answer_{12};
    NameEntry account_number_{8};
    AccountBalanceBytes balance_{};
    StartKey start_key_;
    FranchiseSession* session_ = nullptr;
    bool clears_input_ = false;
    Stage stage_ = Stage::printing_name;
    enum class Pending { none, name, answer, account };
    Pending pending_ = Pending::none;
    std::uint8_t state3a_ = 1;
    std::uint8_t invalid_countdown_ = 0;
    std::size_t account_erase_index_ = 0;
    SoundEvents sound_events_;
    std::vector<std::uint8_t> rendered_bytes_;
};

} // namespace ghostbusters::game
