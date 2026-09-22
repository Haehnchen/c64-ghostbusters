#pragma once

#include "assets/payload.hpp"
#include "assets/ui_text_data.hpp"
#include "game/drive_controls.hpp"
#include "game/text_script.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ghostbusters::game {

// Ending-owned zero-page and $EAxx state. The runtime text storage includes
// the two possible overflow bytes written by $9B90 after a full 20-byte name.
struct EndingRegisters {
    AccountBalanceBytes temporary_balance54{};
    std::array<std::uint8_t, 4> encoded_account_eac7{};
    std::array<std::uint8_t, 4> scratch23_26{};
    std::uint8_t scratch27 = 0;
    std::array<std::uint8_t, 22> runtime_text_ea14{};
    std::size_t runtime_text_length = 0;
    std::uint8_t cursor_column38 = 1;
    std::uint8_t cursor_row39 = 1;
    std::uint8_t post_failure_ea7a = 0;
    std::uint8_t gate02 = 0;

    [[nodiscard]] bool operator==(const EndingRegisters &) const = default;
};

struct EndingInput {
    std::uint8_t irq_counter08 = 0;
    // Current held-key matrix position from $19, not translated edge $17.
    std::uint8_t raw_key19 = 0xFF;
};

enum class EndingAudioAction { ending_reset, text_tone, speech_start };

struct EndingAudioEvent {
    EndingAudioAction action;
    std::uint8_t command = 0;

    [[nodiscard]] bool operator==(const EndingAudioEvent &) const = default;
};

struct EndingTickResult {
    std::vector<EndingAudioEvent> audio_events{};
    bool blocking_request = false;
    bool restart_requested = false;
};

// Native boundary for States 45-63 at $8C74-$8D85. Script output precedes
// handler dispatch on each common frame, as in $72D3-$747B.
class Ending {
  public:
    Ending(const assets::Payload &payload, DriveControlsState state,
           std::array<std::uint8_t, 20> saved_name, EndingRegisters registers);

    using BeforeHandler = std::function<void(DriveControlsState &)>;

    [[nodiscard]] EndingTickResult tick(EndingInput input = {},
                                        const BeforeHandler &before_handler = {});
    [[nodiscard]] EndingTickResult resume_speech();

    [[nodiscard]] DriveControlsState &state() noexcept { return state_; }
    [[nodiscard]] const DriveControlsState &state() const noexcept { return state_; }
    [[nodiscard]] EndingRegisters &registers() noexcept { return registers_; }
    [[nodiscard]] const EndingRegisters &registers() const noexcept { return registers_; }
    [[nodiscard]] const std::array<std::uint8_t, 20> &saved_name() const noexcept {
        return saved_name_;
    }
    [[nodiscard]] std::uint8_t raw_state() const noexcept { return state_.city.state3a; }
    [[nodiscard]] bool script_active() const noexcept { return script_.active(); }
    [[nodiscard]] std::uint8_t active_script_id() const noexcept { return active_script_id_; }
    [[nodiscard]] bool speech_blocked() const noexcept { return blocked_speech_ != 0; }

  private:
    void advance_state();
    void ending_reset(EndingTickResult &result);
    void start_script(assets::UiScript id);
    void start_runtime_script(std::span<const std::uint8_t> bytes);
    void build_display_name();
    void build_money_text(AccountBalanceBytes balance);
    void build_account_text();
    void sync_cursor() noexcept;
    void collect_text_audio(EndingTickResult &result);
    [[nodiscard]] EndingTickResult dispatch(EndingInput input);

    assets::UiTextData text_data_;
    DriveControlsState state_;
    std::array<std::uint8_t, 20> saved_name_{};
    EndingRegisters registers_;
    TextScript script_;
    std::uint8_t active_script_id_ = 0;
    std::uint8_t blocked_speech_ = 0;
};

} // namespace ghostbusters::game
