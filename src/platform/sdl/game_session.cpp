#include "platform/sdl/application.hpp"
#include "platform/sdl/scene_view.hpp"

#include <algorithm>
#include <stdexcept>

namespace ghostbusters::platform::sdl {

ghostbusters::video::IndexedImage Application::with_footer(ghostbusters::video::IndexedImage result, bool visible)
{
    scroller.write_row(payload, footer_characters);
    ghostbusters::video::composite_footer(result, footer_characters, scroller.phase,
                                          title.footer_rainbow(), ui_data.footer_colors(), visible);
    return result;
}

void Application::drive_prefix(ghostbusters::game::DriveControlsState& state)
{
    if (live_prefix_done) return;
    if (state.city.countdown7c != 0) --state.city.countdown7c;
    ghostbusters::game::update_drive_frame(payload, state, drive_d016);
}

std::uint8_t Application::joystick_input()
{ return sampled_joystick; }

std::optional<std::uint8_t> Application::update_common_city_frame(ghostbusters::game::CityControlsState& state,
                                        std::uint8_t key, bool countdown_done, bool defer_after)
{
    state.sprites.enabled_mask = 0xFF; // Common frames restore all sprite enables.
    if (!live_prefix_done && !countdown_done && state.countdown7c != 0) --state.countdown7c;
    // Shared input gates run before dispatch; reset execution remains
    // a separate lifecycle boundary. Do not decrement delay14 twice.
    const ghostbusters::game::CityFrameInput input{frame09, input_frame.gate02, pause_flags47, 0, key, random06};
    const auto pre = ghostbusters::game::update_city_frame_before_notice(payload, state, input);
    if (!pre.continue_frame) return std::nullopt;
    state.notices.tick(state.state3a, state.characters);
    state.characters = ghostbusters::game::update_city_status(state.state3a,
        state.notices.position4b() != 0,
        {state.pk_low5a, state.pk_high5b, state.balance57}, state.characters);
    // $72BD-$72C7 follows the status display even while a notice is active.
    ghostbusters::game::update_city_difficulty(payload, state);
    if (!defer_after) ghostbusters::game::update_city_frame_after_notice(payload, state, input);
    return pre.key17;
}

void Application::tick_city_controls(std::uint8_t joystick, std::uint8_t key)
{
    if (const auto translated = update_common_city_frame(city_controls->state(), key))
        city_controls->tick({random06, frame09, joystick, *translated});
}

void Application::tick_marshmallow(std::uint8_t key)
{
    if (!marshmallow) marshmallow.emplace(payload, city_controls->state(),
        ghostbusters::game::MarshmallowRegisters{0, 0, 0});
    auto& state = marshmallow->state();
    if (const auto translated = update_common_city_frame(state, key)) {
        state.key17 = *translated;
        marshmallow->tick();
    }
    if (state.state3a == 18) {
        city_controls.emplace(payload, state);
        marshmallow.reset();
    }
}

ghostbusters::game::DriveControlsTickResult Application::tick_drive_controls(std::uint8_t joystick, std::uint8_t key, bool effect_busy)
{
    ghostbusters::game::DriveControlsTickResult result;
    auto& state = drive_controls->state();
    if (drive_controls->transition() != ghostbusters::game::DriveControlsTransition::driving) {
        return result;
    }
    drive_prefix(state);
    if (const auto translated = update_common_city_frame(state.city, key, true)) {
        state.city.key17 = *translated;
        result = drive_controls->tick({random06, joystick, effect_busy});
    }
    drive_persistent = {state.direction64, state.animation65, state.fire_latch13,
                        state.roamer_source73, state.capture_timer75,
                        state.scroll_position1a, state.speed1b, state.scroll_fraction1c,
                        state.vehicle_position63, state.distance67};
    return result;
}

void Application::begin_building_entry()
{
    // First city entry clears $1E; the normal active frame has $D011=$1B.
    // $37 is cleared by State22 before any subsequent scene reads it.
    const ghostbusters::game::BuildingEntryRegisters registers{0, 0, 0x1B};
    if (drive_controls) building_entry.emplace(payload, *drive_controls, registers);
    else building_entry.emplace(payload, city_controls->state(), city->vehicle(),
                                drive_persistent, registers);
}

void Application::tick_building_entry(std::uint8_t key)
{
    if (building_entry->stage() == ghostbusters::game::BuildingEntry::Stage::ready) return;
    auto& state = building_entry->state();
    drive_prefix(state);
    if (update_common_city_frame(state.city, key, true)) building_entry->tick();
    drive_persistent = {state.direction64, state.animation65, state.fire_latch13,
                        state.roamer_source73, state.capture_timer75,
                        state.scroll_position1a, state.speed1b, state.scroll_fraction1c,
                        state.vehicle_position63, state.distance67};
    // States22/23 neither start nor stop audio: the existing player continues.
}

void Application::begin_building_controls()
{
    building_persistent.state1e = building_entry->state1e();
    building_persistent.scratch37 = building_entry->scratch37();
    building_controls.emplace(payload, building_entry->state(), building_persistent);
}

void Application::tick_building_controls(std::uint8_t joystick, std::uint8_t key)
{
    auto& state = building_controls->state();
    if (state.city.state3a < 24 || state.city.state3a > 26) return;
    drive_prefix(state);
    if (const auto translated = update_common_city_frame(state.city, key, true)) {
        state.city.key17 = *translated;
        building_controls->tick({random06, frame09, joystick});
    }
    building_persistent = building_controls->registers();
}

ghostbusters::game::BeamControlsTickResult Application::tick_beam_controls(std::uint8_t joystick, std::uint8_t key)
{
    ghostbusters::game::BeamControlsTickResult result;
    if (!beam_controls) {
        beam_controls.emplace(payload, building_controls->state(), building_persistent, beam_persistent);
        // The original has one $3A state. Retire the placement owner when
        // State27 takes over; it must never restart after catch/return.
        building_controls.reset();
    }
    auto& state = beam_controls->state();
    if (state.city.state3a != 27 || beam_controls->pending_irqs() != 0) return result;
    drive_prefix(state);
    if (const auto translated = update_common_city_frame(state.city, key, true)) {
        state.city.key17 = *translated;
        result = beam_controls->tick({random06, frame09, joystick});
    }
    building_persistent = beam_controls->building_registers();
    beam_persistent = beam_controls->registers();
    return result;
}

void Application::sync_zuul()
{
    building_persistent = zuul->building_registers();
    beam_persistent.beam_phase79 = zuul->registers().beam_phase79;
    beam_persistent.beam_scratch7a = zuul->registers().beam_scratch7a;
}

void Application::consume_zuul_reads(const ghostbusters::game::ZuulTickResult& result)
{
    if (result.collision_read || result.collision_clear_read) collision_latch = 0;
    sync_zuul();
}

ghostbusters::game::ZuulTickResult Application::tick_zuul(std::uint8_t joystick, std::uint8_t key, std::uint8_t irq_counter)
{
    ghostbusters::game::ZuulTickResult result;
    if (!zuul) {
        building_persistent.state1e = building_entry->state1e();
        building_persistent.scratch37 = building_entry->scratch37();
        zuul.emplace(payload, building_entry->state(), building_persistent,
            ghostbusters::game::ZuulRegisters{0, 0, 0, 0, 0, 0,
                beam_persistent.beam_phase79, beam_persistent.beam_scratch7a});
    }
    if (zuul->raw_state() > 44) return result; // Ending owns the subsequent states.
    auto& state = zuul->state();
    state.city.sprites.enabled_mask = 0xFF;
    // One stable sprite snapshot per PAL frame. Raster multiplexing and
    // subframe latch timing remain separate from the proven pixel overlap.
    collision_latch |= ghostbusters::video::sprite_collision_mask(scene_sprites(state.city.sprites));
    drive_prefix(state);
    if (const auto translated = update_common_city_frame(state.city, key, true)) {
        state.city.key17 = *translated;
        result = zuul->tick({irq_counter, frame09, joystick, collision_latch});
    }
    consume_zuul_reads(result);
    return result;
}

void Application::sync_catch()
{
    building_persistent = catch_sequence->building_registers();
    beam_persistent = catch_sequence->beam_registers();
    catch_persistent = catch_sequence->registers();
}

ghostbusters::game::CatchSequenceTickResult Application::tick_catch(std::uint8_t key)
{
    if (!catch_sequence) {
        catch_sequence.emplace(payload, beam_controls->state(), building_persistent,
                               beam_persistent, catch_persistent);
        beam_controls.reset();
    }
    auto& state = catch_sequence->state();
    ghostbusters::game::CatchSequenceTickResult result;
    drive_prefix(state);
    if (const auto translated = update_common_city_frame(state.city, key, true)) {
        state.city.key17 = *translated;
        result = catch_sequence->tick({random06, frame09});
    }
    sync_catch();
    return result;
}

int Application::capture_dispatch()
{
    if (catch_sequence && (catch_sequence->state().city.state3a == 17 ||
                           catch_sequence->state().city.state3a == 32)) return 32;
    if (catch_sequence || (beam_controls &&
        (beam_controls->state().city.state3a == 28 || beam_controls->state().city.state3a == 29))) return 28;
    if (beam_controls || (building_controls && building_controls->state().city.state3a == 27)) return 27;
    if (building_return || (building_controls && building_controls->state().city.state3a == 32)) return 32;
    return 24;
}

void Application::restore_city(ghostbusters::game::DriveControlsState& state)
{
    ghostbusters::game::CityEntry::redraw(payload, state.city);
    building_persistent.state1e = 0;
    building_persistent.scratch37 = 0;
    building_persistent.animation77[0] = 0;
    drive_persistent = {state.direction64, state.animation65, state.fire_latch13,
                        state.roamer_source73, state.capture_timer75,
                        state.scroll_position1a, state.speed1b, state.scroll_fraction1c,
                        state.vehicle_position63, state.distance67};
    city_controls.emplace(payload, state.city);
    building_return.reset();
    headquarters.reset();
    catch_sequence.reset();
    beam_controls.reset();
    building_controls.reset();
    building_entry.reset();
    drive_controls.reset();
    drive_entry.reset();
}

void Application::tick_headquarters(std::uint8_t key)
{
    if (!headquarters) {
        building_persistent.state1e = building_entry->state1e();
        building_persistent.scratch37 = building_entry->scratch37();
        headquarters.emplace(payload, building_entry->state(), building_persistent,
            ghostbusters::game::HeadquartersRegisters{city->traps6a(), catch_persistent.full_traps6c, 0});
    }
    auto& state = headquarters->state();
    drive_prefix(state);
    if (const auto translated = update_common_city_frame(state.city, key, true)) {
        state.city.key17 = *translated;
        if (state.city.state3a == 17) {
            restore_city(state);
            return;
        }
        headquarters->tick({frame09});
    }
    building_persistent = headquarters->building_registers();
    catch_persistent.full_traps6c = headquarters->registers().full_traps6c;
}

void Application::tick_building_return(std::uint8_t key)
{
    // Escape goes directly to redraw17; BuildingReturn accepts only32/33.
    if (catch_sequence && catch_sequence->state().city.state3a == 17) {
        auto& state = catch_sequence->state();
        drive_prefix(state);
        if (update_common_city_frame(state.city, key, true)) restore_city(state);
        return;
    }
    if (!building_return) {
        if (catch_sequence) {
            building_return.emplace(payload, catch_sequence->state(), building_persistent);
            catch_sequence.reset();
        } else {
            building_return.emplace(payload, *building_controls);
            building_controls.reset();
        }
    }
    auto& state = building_return->state();
    drive_prefix(state);
    const auto translated = update_common_city_frame(state.city, key, true);
    if (!translated) return;
    state.city.key17 = *translated;
    if (state.city.state3a == 17) {
        building_persistent = building_return->registers();
        restore_city(state);
    } else {
        building_return->tick({frame09});
        building_persistent = building_return->registers();
    }
    // States32/33 and city redraw17 preserve the running audio players.
}

void Application::begin_ending()
{
    ghostbusters::game::DriveControlsState state;
    if (zuul && zuul->raw_state() > 44) state = zuul->state();
    else state.city = city_controls->state();
    ghostbusters::game::EndingRegisters registers;
    registers.gate02 = 0x80;
    registers.encoded_account_eac7 = replay_session.franchise.packed_account;
    std::array<std::uint8_t, 20> saved_name{};
    if (dialog) saved_name = dialog->name().bytes();
    else if (diagnostic_name) saved_name = *diagnostic_name;
    else throw std::logic_error("Ending requires a saved franchise name");
    ending.emplace(payload, std::move(state), saved_name, registers);
}

ghostbusters::game::EndingTickResult Application::tick_ending(std::uint8_t raw_key, std::uint8_t irq_counter)
{
    if (!ending) begin_ending();
    auto& state = ending->state();
    drive_prefix(state);
    ghostbusters::game::EndingTickResult result;
    if (update_common_city_frame(state.city, city_key, true, true)) {
        result = ending->tick({irq_counter, raw_key}, [&](auto& current) {
            ghostbusters::game::update_city_frame_after_notice(payload, current.city,
                {frame09, ending->registers().gate02, 0, 0, 0, random06});
        });
        replay_session.franchise.packed_account = ending->registers().encoded_account_eac7;
    }
    return result;
}

ghostbusters::game::DriveControlsState* Application::active_drive_state()
{
    if (ending) return &ending->state();
    if (zuul) return &zuul->state();
    if (headquarters) return &headquarters->state();
    if (building_return) return &building_return->state();
    if (catch_sequence) return &catch_sequence->state();
    if (beam_controls) return &beam_controls->state();
    if (building_controls) return &building_controls->state();
    if (building_entry) return &building_entry->state();
    if (drive_controls) return &drive_controls->state();
    return nullptr;
}

ghostbusters::game::CityControlsState* Application::active_city_state()
{
    if (marshmallow) return &marshmallow->state();
    if (auto* state = active_drive_state()) return &state->city;
    if (drive_entry) return &drive_entry->state();
    if (city_controls) return &city_controls->state();
    return nullptr;
}

std::uint8_t Application::original_scene_state()
{
    if (start_phase != StartPhase::running) return 0;
    if (const auto* state = active_city_state()) return state->state3a;
    if (city) return city->stage() == ghostbusters::game::CityEntry::Stage::leaving_shop ? 16 : 17;
    if (equipment) return equipment->original_state();
    if (vehicle) return vehicle->original_state();
    return dialog ? dialog->original_state() : 255;
}

void Application::start_scene_speech(ghostbusters::video::CharacterFrame& characters,
                                     ghostbusters::audio::SceneAudio& player,
                                     std::uint8_t command)
{
    // Live play and diagnostics must switch presentation before starting speech.
    ghostbusters::game::begin_gameplay_speech(characters);
    player.start_speech(command);
}

void Application::apply_ending_audio(const ghostbusters::game::EndingTickResult& result,
                                   ghostbusters::audio::SceneAudio& player)
{
    for (const auto& event : result.audio_events) {
        if (event.action == ghostbusters::game::EndingAudioAction::ending_reset)
            player.enter_ending();
        else if (event.action == ghostbusters::game::EndingAudioAction::text_tone)
            player.text_tone();
        else {
            start_scene_speech(ending->state().city.characters, player, event.command);
        }
    }
}

ghostbusters::video::IndexedImage Application::scene_content_image()
{
    if (start_phase != StartPhase::running)
        return ghostbusters::video::render_characters(reset_state.characters);
    if (ending) {
        const auto& state = ending->state().city;
        return scene_image(state.characters, state.sprites, state.notices.phase49(), std::nullopt, true);
    }
    if (zuul) {
        const auto& state = zuul->state().city;
        using ghostbusters::game::ZuulTransition;
        const auto stage = zuul->transition();
        const bool rooftop = stage == ZuulTransition::gate ||
                             stage == ZuulTransition::climb_setup ||
                             stage == ZuulTransition::climb;
        return scene_image(state.characters, state.sprites, state.notices.phase49(),
                           std::nullopt, true, rooftop ? 0 : 6);
    }
    if (marshmallow) {
        const auto& state = marshmallow->state();
        return scene_image(state.characters, state.sprites, state.notices.phase49());
    }
    if (headquarters) {
        const auto& state = headquarters->state().city;
        return scene_image(state.characters, state.sprites, state.notices.phase49(), std::nullopt, true);
    }
    if (catch_sequence) {
        const auto& state = catch_sequence->state().city;
        return scene_image(state.characters, state.sprites, state.notices.phase49(), std::nullopt, true);
    }
    if (beam_controls) {
        const auto& state = beam_controls->state().city;
        return scene_image(state.characters, state.sprites, state.notices.phase49(), std::nullopt, true);
    }
    if (building_return) {
        const auto& state = building_return->state().city;
        return scene_image(state.characters, state.sprites, state.notices.phase49(), std::nullopt, true);
    }
    if (building_controls) {
        const auto& state = building_controls->state().city;
        return scene_image(state.characters, state.sprites, state.notices.phase49(), std::nullopt, true);
    }
    if (building_entry) {
        const auto& state = building_entry->state().city;
        return scene_image(state.characters, state.sprites, state.notices.phase49(), std::nullopt, true);
    }
    if (drive_controls) {
        const auto& state = drive_controls->state().city;
        return scene_image(state.characters, state.sprites, state.notices.phase49(), drive_d016);
    }
    if (drive_entry) {
        const auto& state = drive_entry->state();
        return scene_image(state.characters, state.sprites, state.notices.phase49());
    }
    if (city_controls) {
        const auto& state = city_controls->state();
        return scene_image(state.characters, state.sprites, state.notices.phase49());
    }
    if (city) return scene_image(*city_display, city->sprites());
    if (equipment) return equipment_image(*equipment);
    auto result = ghostbusters::video::render_game_scene(
        vehicle ? vehicle->characters() : dialog->characters(), {});
    // Main dialogue background differs from the lower IRQ/scroller band.
    std::fill(result.begin() + 186 * 320, result.end(), 0);
    return result;
}

ghostbusters::video::IndexedImage Application::dialog_image()
{
    const bool speech_blocked = (catch_sequence && catch_sequence->speech_blocked()) ||
                                (zuul && zuul->speech_blocked()) ||
                                (ending && ending->speech_blocked());
    return with_footer(scene_content_image(), !speech_blocked);
}

} // namespace ghostbusters::platform::sdl
