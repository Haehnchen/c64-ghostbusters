#include "game/ending.hpp"

#include "game/account_codec.hpp"
#include "game/money_text.hpp"
#include "game/sound_event.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ghostbusters::game {
namespace {

[[nodiscard]] bool is_letter(std::uint8_t value) noexcept { return value >= 0x41 && value < 0x5B; }

} // namespace

Ending::Ending(const assets::Payload &payload, DriveControlsState state,
               std::array<std::uint8_t, 20> saved_name, EndingRegisters registers)
    : text_data_(payload), state_(std::move(state)), saved_name_(saved_name), registers_(registers) {
    if (raw_state() < 45 || raw_state() > 63) {
        throw std::invalid_argument("Ending requires State 45-63");
    }
    if (registers_.cursor_column38 >= 40 || registers_.cursor_row39 >= 25) {
        throw std::out_of_range("Ending text cursor is outside the 40x25 screen");
    }
    script_.set_cursor(registers_.cursor_column38, registers_.cursor_row39);
}

void Ending::sync_cursor() noexcept {
    registers_.cursor_column38 = script_.column();
    registers_.cursor_row39 = script_.row();
}

void Ending::advance_state() {
    state_.city.key17 = 0;
    state_.city.state3a = static_cast<std::uint8_t>(state_.city.state3a + 1U);
}

void Ending::ending_reset(EndingTickResult &result) {
    auto &frame = state_.city.characters;
    frame.background = 8;
    std::fill_n(frame.screen.begin(), 0x3C0, 0);
    std::fill_n(frame.colors.begin(), 0x3C0, 0);
    std::fill_n(frame.colors.begin() + 0x370, 0x27, 1);
    state_.city.sprites.x.fill(0);
    state_.city.sprites.y.fill(0);
    result.audio_events.push_back({EndingAudioAction::ending_reset, 0});
}

void Ending::start_script(assets::UiScript id) {
    active_script_id_ = static_cast<std::uint8_t>(id);
    script_.start(text_data_.script(id), registers_.cursor_column38, registers_.cursor_row39, 0);
}

void Ending::start_runtime_script(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > registers_.runtime_text_ea14.size() ||
        bytes.back() != 0xFF) {
        throw std::runtime_error("Ending runtime script is invalid");
    }
    if (bytes.data() != registers_.runtime_text_ea14.data())
        std::copy(bytes.begin(), bytes.end(), registers_.runtime_text_ea14.begin());
    registers_.runtime_text_length = bytes.size();
    active_script_id_ = 0x80;
    script_.start(bytes, registers_.cursor_column38, registers_.cursor_row39, 1);
}

void Ending::build_display_name() {
    auto &runtime = registers_.runtime_text_ea14;
    std::fill_n(runtime.begin(), 20, 0);

    std::size_t source = 0;
    while (source < saved_name_.size() && saved_name_[source] != ',')
        ++source;
    std::size_t output = 0;
    if (source < saved_name_.size()) {
        ++source;
        while (source < saved_name_.size())
            runtime[output++] = saved_name_[source++];
    }

    std::size_t end = 20;
    while (end != 0 && runtime[end - 1U] == 0)
        --end;
    output = end;
    runtime[output++] = 0x20;
    source = 0;
    while (output < 20 && source < saved_name_.size() && is_letter(saved_name_[source])) {
        runtime[output++] = saved_name_[source++];
    }
    runtime[output++] = 0xFF;
    start_runtime_script(std::span<const std::uint8_t>(runtime.data(), output));
}

void Ending::build_money_text(AccountBalanceBytes balance) {
    registers_.scratch23_26[0] = balance.byte57;
    registers_.scratch23_26[1] = balance.byte58;
    registers_.scratch23_26[2] = balance.byte59;
    registers_.scratch27 = 3;

    // Preserve all eight raw workspace bytes before moving the compact script
    // over its front; bytes beyond the moved terminator remain unchanged.
    const auto workspace = format_money_field(balance);
    std::copy(workspace.begin(), workspace.end(), registers_.runtime_text_ea14.begin());

    const auto text = format_money(balance);
    start_runtime_script(text);
}

void Ending::build_account_text() {
    registers_.encoded_account_eac7 = encodeAccount(saved_name_, state_.city.balance57);
    registers_.scratch23_26 = registers_.encoded_account_eac7;
    registers_.scratch27 = 4;
    std::array<std::uint8_t, 9> text{};
    for (std::size_t index = 0; index < registers_.encoded_account_eac7.size(); ++index) {
        const auto value = registers_.encoded_account_eac7[index];
        text[index * 2U] = static_cast<std::uint8_t>(0x30U | (value >> 4U));
        text[index * 2U + 1U] = static_cast<std::uint8_t>(0x30U | (value & 0x0FU));
    }
    text.back() = 0xFF;
    start_runtime_script(text);
}

void Ending::collect_text_audio(EndingTickResult &result) {
    for (const auto event : script_.take_sound_events()) {
        if (event != SoundEvent::text_tone) {
            throw std::logic_error("Ending received an unexpected text-script sound");
        }
        result.audio_events.push_back({EndingAudioAction::text_tone, 0});
    }
}

EndingTickResult Ending::dispatch(EndingInput input) {
    EndingTickResult result;
    switch (raw_state()) {
    case 45:
        // The unprofitable ending reports both balances unchanged and skips
        // account generation; the shared credit/account path starts at 46.
        ending_reset(result);
        start_script(assets::UiScript::poor_ending);
        state_.city.state3a = 59;
        registers_.post_failure_ea7a = 0x3B;
        break;
    case 46:
        ending_reset(result);
        registers_.post_failure_ea7a = 0;
        start_script(assets::UiScript::failure_prefix);
        advance_state();
        break;
    case 47:
        build_display_name();
        advance_state();
        break;
    case 48:
        start_script(assets::UiScript::failure_suffix);
        advance_state();
        break;
    case 49:
        start_script(assets::UiScript::credit_prefix);
        advance_state();
        break;
    case 50:
        // Saved accounts carry only the two high balance bytes, so the credit
        // path permanently discards the low byte before display and encoding.
        state_.city.balance57.byte59 = 0;
        build_money_text(state_.city.balance57);
        advance_state();
        break;
    case 51:
        start_script(assets::UiScript::account_prefix);
        advance_state();
        break;
    case 52:
        build_account_text();
        advance_state();
        break;
    case 53:
        start_script(assets::UiScript::restart_prompt);
        state_.city.state3a = 57;
        break;
    case 54:
        ending_reset(result);
        registers_.post_failure_ea7a = 0;
        start_script(assets::UiScript::success_prefix);
        advance_state();
        break;
    case 55:
        build_display_name();
        advance_state();
        break;
    case 56:
        start_script(assets::UiScript::success_suffix);
        state_.city.state3a = 49;
        break;
    case 57:
        if (registers_.post_failure_ea7a != 0) {
            blocked_speech_ = 3;
            result.audio_events.push_back({EndingAudioAction::speech_start, 3});
            result.blocking_request = true;
            break;
        }
        registers_.gate02 = 0xFF;
        advance_state();
        break;
    case 58:
        // Restart listens to held raw matrix positions, not translated key
        // edges; Return and Space therefore cannot leave the ending.
        result.restart_requested = input.raw_key19 == 0x20 || input.raw_key19 == 0x28;
        break;
    case 59:
        start_script(assets::UiScript::starting_balance);
        advance_state();
        break;
    case 60:
        registers_.temporary_balance54 = state_.city.balance57;
        state_.city.balance57 = state_.city.starting_balance51;
        build_money_text(state_.city.balance57);
        advance_state();
        break;
    case 61:
        start_script(assets::UiScript::ending_balance);
        advance_state();
        break;
    case 62:
        state_.city.balance57 = registers_.temporary_balance54;
        build_money_text(state_.city.balance57);
        advance_state();
        break;
    case 63:
        state_.city.state3a = 57;
        break;
    default:
        throw std::logic_error("Ending state left its owned range");
    }
    sync_cursor();
    return result;
}

EndingTickResult Ending::tick(EndingInput input, const BeforeHandler &before_handler) {
    if (blocked_speech_ != 0)
        return {{}, true, false};
    if (script_.active()) {
        EndingTickResult result;
        script_.tick(state_.city.characters, input.irq_counter08, 3);
        collect_text_audio(result);
        sync_cursor();
        if (script_.active())
            return result;
        if (before_handler)
            before_handler(state_);
        auto dispatched = dispatch(input);
        result.audio_events.insert(result.audio_events.end(), dispatched.audio_events.begin(),
                                   dispatched.audio_events.end());
        result.blocking_request = dispatched.blocking_request;
        result.restart_requested = dispatched.restart_requested;
        return result;
    }
    if (before_handler)
        before_handler(state_);
    return dispatch(input);
}

EndingTickResult Ending::resume_speech() {
    if (blocked_speech_ == 0)
        return {};
    blocked_speech_ = 0;
    registers_.gate02 = 0xFF;
    advance_state();
    sync_cursor();
    return {};
}

} // namespace ghostbusters::game
