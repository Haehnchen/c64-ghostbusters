#include "platform/sdl/application.hpp"
#include "platform/sdl/scene_view.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace ghostbusters::platform::sdl {
namespace {
void checked(bool success) {
    if (!success) throw std::runtime_error(SDL_GetError());
}
}
int Application::run()
{
    if (!options.diagnostic_mode.empty()) return run_diagnostic();
    const auto smoke = options.smoke;
    const auto& input_replay_path = options.input_replay_path;
    const auto& input_trace_path = options.input_trace_path;
    const auto play_from_state = options.play_from_state;
    checked(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO));
    auto& input_replay = replay_session.input;
    if (!input_replay_path.empty() && !input_replay && !replay_session.lead_in_complete)
        input_replay.emplace(input_replay_path);
    ghostbusters::audio::SceneAudio audio(payload);
    bool title_startup_pending = true;
    if (!input_trace_path.empty()) audio.enable_sid_trace();
    ghostbusters::platform::sdl::AudioOutput audio_output;
    auto apply_beam_audio = [&](const ghostbusters::game::BeamControlsTickResult& result) {
        for (const auto event : result.audio_events) {
            if (event.action == ghostbusters::game::BeamAudioAction::voice3_stop) audio.voice3_effect_stop();
            else audio.voice3_effect_start(event.effect);
        }
    };
    auto apply_zuul_audio = [&](const ghostbusters::game::ZuulTickResult& result) {
        for (const auto& event : result.audio_events) {
            start_scene_speech(zuul->state().city.characters, audio, event.command);
        }
    };
    auto apply_scene_audio_events = [&] {
        auto apply_events = [&](const ghostbusters::game::SoundEvents& events) {
            for (const auto event : events) {
                if (event == ghostbusters::game::SoundEvent::text_tone) audio.text_tone();
                else if (event == ghostbusters::game::SoundEvent::key_click) audio.key_click();
                else if (event == ghostbusters::game::SoundEvent::equipment_motor_start) audio.equipment_motor_start();
                else if (event == ghostbusters::game::SoundEvent::equipment_motor_stop) audio.equipment_motor_stop();
            }
        };
        if (dialog) apply_events(dialog->take_sound_events());
        if (vehicle) apply_events(vehicle->take_sound_events());
        if (equipment) apply_events(equipment->take_sound_events());
    };
    auto& audio_samples = replay_session.audio_samples;
    auto& sid_writes = replay_session.sid_writes;
    auto& audio_energy = replay_session.audio_energy;
    auto output_audio = [&](const std::vector<float>& pcm) {
        const bool title_active = !dialog && start_phase == StartPhase::running;
        scroller.tick(title_active ? (audio.speech_active() ? 0x80 : 0) : input_frame.gate02);
        if (!input_trace_path.empty()) {
            audio_samples += pcm.size();
            sid_writes += audio.take_sid_writes().size();
            for (const auto sample : pcm) audio_energy += double(sample) * sample;
        }
        if (input_replay) return; // Accelerated replay needs no real-time queue.
        // Discard queues longer than 100 ms at 48 kHz after a debugger pause
        // or display stall.
        if (audio_output.queued_frames() > 4800) audio_output.clear();
        audio_output.push(pcm);
    };
    auto audio_frame = [&](bool common_frame = true) {
        audio.begin_frame(common_frame, pause_flags47);
        if (common_frame) apply_scene_audio_events();
        output_audio(audio.finish_frame());
    };
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        // Replays still render every frame, at native pixel size without the
        // interactive window's 3x presentation scaling.
        SDL_CreateWindow(ghostbusters::platform::sdl::kWindowTitle, input_replay ? 320 : 960,
                         input_replay ? 200 : 720, SDL_WINDOW_RESIZABLE |
                         (replay_session.fullscreen ? SDL_WINDOW_FULLSCREEN : 0)),
        SDL_DestroyWindow);
    if (!window) throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
    if (!renderer) throw std::runtime_error(SDL_GetError());
    checked(ghostbusters::platform::sdl::configure_presentation(renderer.get(), bool(input_replay)));
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 200),
        SDL_DestroyTexture);
    if (!texture) throw std::runtime_error(SDL_GetError());
    checked(SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST));
    std::array<std::uint32_t, 320 * 200> pixels{};
    auto upload = [&] {
        for (std::size_t index = 0; index < image.size(); ++index) pixels[index] = palette[image[index]];
        void* destination = nullptr;
        int pitch = 0;
        checked(SDL_LockTexture(texture.get(), nullptr, &destination, &pitch));
        for (unsigned row = 0; row < 200; ++row) {
            std::memcpy(static_cast<std::uint8_t*>(destination) + row * pitch,
                        pixels.data() + row * 320, 320 * sizeof(std::uint32_t));
        }
        SDL_UnlockTexture(texture.get());
    };
    upload();
    ghostbusters::game::PalClock clock;
    std::ofstream input_trace;
    if (!input_trace_path.empty()) {
        input_trace.open(input_trace_path, replay_session.generation == 0
            ? std::ios::out : std::ios::out | std::ios::app);
        if (!input_trace) throw std::runtime_error("Cannot open live input trace");
    }
    auto& input_trace_frame = replay_session.frame;
    bool restart_after_frame = false;
    std::uint8_t replay_sampled_key = 0;
    auto trace_input = [&](std::uint8_t sampled_key) {
        if (!input_trace.is_open()) return;
        input_trace << "{\"frame\":" << input_trace_frame++
            << ",\"replay_active\":" << (input_replay ? "true" : "false")
            << ",\"generation\":" << replay_session.generation
            << ",\"restart_requested\":" << (restart_after_frame ? "true" : "false")
            << ",\"state\":" << unsigned(original_scene_state())
            << ",\"counter08\":" << unsigned(audio.irq_counter())
            << ",\"frame09\":" << unsigned(frame09)
            << ",\"mode20\":" << unsigned(input_frame.mode20)
            << ",\"reset\":" << unsigned(last_reset)
            << ",\"sampled_key17\":" << unsigned(sampled_key)
            << ",\"key17\":" << unsigned(input_scan.key17)
            << ",\"latch18\":" << unsigned(input_scan.latch18)
            << ",\"raw19\":" << unsigned(input_scan.raw19)
            << ",\"joystick33\":" << unsigned(input_scan.joystick33)
            << ",\"idle45\":" << unsigned(input_frame.idle45)
            << ",\"idle46\":" << unsigned(input_frame.idle46)
            << ",\"flags47\":" << unsigned(pause_flags47)
            << ",\"display_enabled\":" << (display_enabled ? "true" : "false")
            << ",\"fullscreen\":" << ((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) ? "true" : "false")
            << ",\"fullscreen_requested\":" << (replay_session.fullscreen ? "true" : "false")
            << ",\"speech\":" << (audio.speech_active() ? "true" : "false")
            << ",\"speech_command\":" << audio.speech_command()
            << ",\"effect\":" << (audio.effect_busy() ? "true" : "false")
            << ",\"audio_samples\":" << audio_samples
            << ",\"audio_energy\":" << audio_energy
            << ",\"sid_writes\":" << sid_writes
            << ",\"city_music\":" << (audio.city_music() ? "true" : "false")
            << ",\"script_active\":" << ((ending ? ending->script_active() :
                original_scene_state() >= 16 ? false : equipment ? equipment->script_active() :
                vehicle ? vehicle->script_active() : dialog ? dialog->script_active() : false) ? "true" : "false")
            << ",\"vehicle\":" << (vehicle ? int(vehicle->selected_vehicle()) : -1)
            << ",\"shop_position\":" << (equipment ? int(equipment->state5f()) : -1)
            << ",\"shop_category\":" << (equipment ? int(equipment->category()) : -1)
            << ",\"carried\":" << (equipment ? int(equipment->carried_slot60()) : -1)
            << ",\"traps\":" << (equipment ? int(equipment->traps6a()) : -1)
            << ",\"purchase_count\":" << (equipment ? int(equipment->purchase_count62()) : -1)
            << ",\"capacity_notice_bytes\":" << (equipment ? equipment->pending_notice().size() : 0)
            << ",\"full_traps\":" << unsigned(catch_sequence ? catch_sequence->registers().full_traps6c :
                headquarters ? headquarters->registers().full_traps6c : catch_persistent.full_traps6c)
            << ",\"name\":[";
        bool first = true;
        const auto name = dialog ? dialog->name().bytes() : std::array<std::uint8_t,20>{};
        for (const auto byte : name) {
            if (!first) input_trace << ',';
            first = false;
            input_trace << unsigned(byte);
        }
        input_trace << ']';
        const auto balance = active_city_state() ? active_city_state()->balance57 :
            equipment ? equipment->balance() : vehicle ? vehicle->balance() :
            dialog ? dialog->balance() : ghostbusters::game::AccountBalanceBytes{};
        input_trace << ",\"balance\":[" << unsigned(balance.byte57) << ','
            << unsigned(balance.byte58) << ',' << unsigned(balance.byte59) << ']'
            << ",\"owned\":" << (active_city_state() ? int(active_city_state()->owned_mask6d) :
                equipment ? int(equipment->owned_mask()) : 0);
        if (const auto* state = active_city_state()) {
            input_trace << ",\"pk\":" << unsigned(state->pk_high5b) * 256 + state->pk_low5a
                << ",\"player_x\":" << unsigned(state->sprites.x[0])
                << ",\"player_y\":" << unsigned(state->sprites.y[0])
                << ",\"building\":" << unsigned(state->current_building6e)
                << ",\"empty_traps\":" << unsigned(state->empty_traps6b)
                << ",\"backup_men\":" << unsigned(state->backup_men3d)
                << ",\"backpack_charge\":" << unsigned(state->backpack_charge3e)
                << ",\"pending_alert\":" << unsigned(state->pending_alert80)
                << ",\"bait\":" << unsigned(state->bait69)
                << ",\"bait_active\":" << unsigned(state->bait_active68)
                << ",\"finale_active\":" << unsigned(state->finale_active81)
                << ",\"notice_position\":" << unsigned(state->notices.position4b())
                << ",\"notice_length\":" << unsigned(state->notices.length4a())
                << ",\"sprite_priority\":" << unsigned(state->sprites.priority_mask)
                << ",\"buildings\":[";
            for (unsigned j = 0; j < state->building_status_c8.size(); ++j) {
                if (j) input_trace << ',';
                input_trace << unsigned(state->building_status_c8[j]);
            }
            input_trace << ']';
            auto sprite_array = [&](const char* name, const auto& values) {
                input_trace << ",\"" << name << "\":[";
                for (unsigned j = 0; j < values.size(); ++j) {
                    if (j) input_trace << ',';
                    input_trace << unsigned(values[j]);
                }
                input_trace << ']';
            };
            sprite_array("sprites_x", state->sprites.x);
            sprite_array("sprites_y", state->sprites.y);
            sprite_array("sprite_targets_x", state->sprites.target_x);
            sprite_array("sprite_targets_y", state->sprites.target_y);
            sprite_array("sprite_pointers", state->sprites.pointers);
            sprite_array("map_types", state->map_types_ea28);
            sprite_array("roamer_contacts", state->roamer_counters6f);
            if (state->state3a == 18 || state->state3a == 36) {
                const ghostbusters::assets::CityData data(payload);
                std::array<std::uint8_t, 20> colors{};
                for (std::size_t i = 0; i < colors.size(); ++i)
                    colors[i] = state->characters.colors.at(data.building_color_base(i));
                sprite_array("building_colors", colors);
            }
            if (ending) sprite_array("ending_account", ending->registers().encoded_account_eac7);
        }
        if (const auto* state = active_drive_state()) {
            input_trace << ",\"drive_vehicle\":" << unsigned(state->vehicle5c)
                << ",\"drive_speed\":" << unsigned(state->speed1b)
                << ",\"drive_distance\":" << unsigned(state->distance67)
                << ",\"drive_position\":" << unsigned(state->vehicle_position63)
                << ",\"drive_capture_timers\":[" << unsigned(state->capture_timer75[0])
                << ',' << unsigned(state->capture_timer75[1]) << ']';
        } else if (drive_entry) {
            input_trace << ",\"drive_vehicle\":" << unsigned(drive_entry->vehicle())
                << ",\"drive_speed\":" << unsigned(drive_entry->state1b())
                << ",\"drive_distance\":" << unsigned(drive_entry->distance67())
                << ",\"drive_position\":" << unsigned(drive_entry->vehicle_position63());
        }
        if (original_scene_state() == 255) {
            // Accelerated replay traces are emitted after rendering; live
            // per-tick traces precede it and must not report stale pixels.
            if (input_replay)
                input_trace << ",\"title_footer_pixels\":"
                            << std::count_if(image.begin() + 192 * 320, image.end(),
                                             [](auto pixel) { return pixel != 0; });
            input_trace << ",\"title_gate\":" << unsigned(title_sequence.gate())
                        << ",\"title_timeline\":" << title_sequence.timeline_index()
                        << ",\"title_text_cursor\":" << title_sequence.text_cursor()
                        << ",\"title_pending_space\":" << (title_sequence.pending_space() ? "true" : "false")
                        << ",\"title_scroller\":" << scroller.phase
                        << ",\"title_ball_x\":" << title_ball.x_register()
                        << ",\"title_ball_y\":" << unsigned(title_ball.y_register())
                        << ",\"title_rows\":[";
            for (unsigned index = 800; index < 920; ++index) {
                if (index != 800) input_trace << ',';
                input_trace << unsigned(title.characters.screen[index]);
            }
            input_trace << ']';
        }
        input_trace << "}\n";
        input_trace.flush();
    };
    auto last_time = SDL_GetTicksNS();
    if (input_replay && play_from_state)
        std::cout << "Playing the natural route to " << options.play_from_name
                  << " (accelerated, muted). Escape cancels.\n" << std::flush;
    bool running = true;
    while (running) {
        replay_sampled_key = 0;
        if (input_replay) input_replay->queue_edges();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
                continue;
            }
            if (input_replay &&
                (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                event.key.which != ghostbusters::platform::sdl::InputReplay::keyboard_id) continue;
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_F11 && !event.key.repeat) {
                    const bool fullscreen = !replay_session.fullscreen;
                    checked(ghostbusters::platform::sdl::set_borderless_fullscreen(window.get(), fullscreen));
                    replay_session.fullscreen = fullscreen;
                    continue;
                }
                if (!event.key.repeat && !dialog && start_phase == StartPhase::running && !audio.speech_active() &&
                    (event.key.key == SDLK_F1 || event.key.key == SDLK_F3)) {
                    const auto key = event.key.key == SDLK_F1 ? ghostbusters::game::StartKey::f1
                                                            : ghostbusters::game::StartKey::f3;
                    start_key = key;
                    start_phase = StartPhase::reset_requested;
                    input_frame.mode20 = 1; // $6FE2, consumed by $7073.
                    reset_state.characters = title.characters;
                    // $6FE9 clears the visible foreground before the first
                    // common frame; the IRQ/scroller tail survives.
                    std::fill_n(reset_state.characters.screen.begin(), 0x3C0, 0);
                    std::fill_n(reset_state.characters.colors.begin(), 0x3C0, 0);
                    audio.leave_title();
                    audio_output.clear();
                    image = dialog_image();
                    upload();
                }
            }
        }
        if (!running) break;
        const auto now = SDL_GetTicksNS();
        const auto ticks = input_replay ? 1U : clock.advance(std::chrono::nanoseconds(now - last_time));
        last_time = now;
        if (ticks != 0) {
            if (dialog || start_phase != StartPhase::running) {
                for (unsigned i = 0; i < ticks; ++i) {
                    if (ending && ending->speech_blocked()) {
                        audio_frame(false);
                        if (!audio.speech_active()) {
                            ghostbusters::game::end_gameplay_speech(ending->state().city.characters);
                            apply_ending_audio(ending->resume_speech(), audio);
                        }
                        continue;
                    }
                    if (zuul && !ending && zuul->speech_blocked()) {
                        collision_latch |= ghostbusters::video::sprite_collision_mask(scene_sprites(zuul->state().city.sprites));
                        audio_frame(false);
                        if (!audio.speech_active()) {
                            ghostbusters::game::end_gameplay_speech(zuul->state().city.characters);
                            const auto resumed = zuul->resume_speech();
                            consume_zuul_reads(resumed);
                            apply_zuul_audio(resumed);
                        }
                        continue;
                    }
                    if (catch_sequence && catch_sequence->speech_blocked()) {
                        audio_frame(false);
                        if (!audio.speech_active()) {
                            ghostbusters::game::end_gameplay_speech(catch_sequence->state().city.characters);
                            (void)catch_sequence->resume_speech();
                            sync_catch();
                        }
                        continue;
                    }
                    random06 = ghostbusters::game::advance_game_random(random06);
                    if (beam_controls && beam_controls->pending_irqs() != 0) {
                        // IRQ music/effect work precedes the suspended foreground's
                        // beam helper. No common frame, input polling or $09 tick.
                        audio_frame(false);
                        apply_beam_audio(beam_controls->resume_irq());
                        building_persistent = beam_controls->building_registers();
                        beam_persistent = beam_controls->registers();
                        continue;
                    }
                    // IRQ sees the previous $47; RUN/STOP is polled only
                    // afterward. Common countdown/drive work also precedes
                    // the pause gate and must continue on stopped frames.
                    audio.begin_frame(false, pause_flags47);
                    // $6FFB samples the previous idle sign before any input
                    // activity can clear it later in this foreground frame.
                    foreground_d011 = ghostbusters::game::foreground_display_control(
                        foreground_d011, input_frame.idle45);
                    display_enabled = (foreground_d011 & 0x10) != 0;
                    live_prefix_done = false;
                    if (auto* state = active_drive_state()) drive_prefix(*state);
                    else if (auto* state = active_city_state()) {
                        if (state->countdown7c != 0) --state->countdown7c;
                    }
                    if (auto* state = active_city_state()) state->sprites.enabled_mask = 0xFF;
                    live_prefix_done = true;
                    auto held = ghostbusters::platform::sdl::HeldInput{};
                    if (input_replay) {
                        for (const auto key : input_replay->keys())
                            ghostbusters::platform::sdl::map_held_key(payload, held, key,
                                original_scene_state() >= 15);
                    } else {
                        held = ghostbusters::platform::sdl::sample_held_input(
                            payload, original_scene_state() >= 15);
                    }
                    if (ending) input_frame.gate02 = ending->registers().gate02;
                    (void)ghostbusters::game::poll_input(payload, input_frame, input_scan,
                        {held.matrix, held.port_a, held.port_b});
                    sampled_joystick = static_cast<std::uint8_t>(input_scan.joystick33 | 0xE0);
                    const auto sampled_key = input_scan.key17;
                    replay_sampled_key = sampled_key;
                    city_key = equipment_key = ui_key = sampled_key;
                    const auto input_result = ghostbusters::game::update_input_frame(
                        input_frame, audio.irq_counter(), original_scene_state(), input_scan.raw19);
                    audio.common_volume(input_result.volume);
                    last_reset = input_result.reset;
                    if (input_result.reset != ghostbusters::game::GameReset::none) {
                        if (start_phase == StartPhase::reset_requested) scroller.phase = 0;
                        if (auto* state = active_city_state()) reset_state.characters = state->characters;
                        else if (city) reset_state.characters = city->characters();
                        else if (equipment) reset_state.characters = equipment->characters();
                        else if (vehicle) reset_state.characters = vehicle->characters();
                        else if (dialog) reset_state.characters = dialog->characters();
                        // The reset leaves the input event, raw key, global clocks,
                        // $14 and mode/gate bytes untouched. Only copy back fields
                        // owned by $33-$DB; audio owns its corresponding subset.
                        reset_state.zero[0x33] = input_scan.joystick33;
                        reset_state.zero[0x34] = input_scan.joystick34;
                        (void)ghostbusters::game::apply_game_reset(payload,input_result.reset,reset_state);
                        input_scan.joystick33 = reset_state.zero[0x33];
                        input_scan.joystick34 = reset_state.zero[0x34];
                        input_frame.idle45 = reset_state.zero[0x45];
                        input_frame.idle46 = reset_state.zero[0x46];
                        input_frame.flags47 = reset_state.zero[0x47];
                        audio.reset_foreground();
                        // The normal IRQ presents the reset's $3B background.
                        reset_state.characters.background = reset_state.zero[0x3B];
                        ending.reset(); zuul.reset(); marshmallow.reset(); headquarters.reset();
                        catch_sequence.reset(); beam_controls.reset(); building_return.reset();
                        building_controls.reset(); building_entry.reset(); drive_controls.reset();
                        drive_entry.reset(); city_controls.reset(); city.reset(); city_display.reset();
                        equipment.reset(); vehicle.reset(); dialog.reset();
                        catch_persistent.deformation7b = catch_persistent.full_traps6c = 0;
                        beam_persistent.beam_phase79 = beam_persistent.beam_scratch7a = 0;
                        building_persistent.scratch37 = building_persistent.direction7d = 0;
                        building_persistent.animation77.fill(0);
                        building_persistent.beam_direction7e.fill(0);
                        drive_persistent.direction64 = drive_persistent.animation65 = 0;
                        drive_persistent.roamer_source73.fill(0);
                        drive_persistent.capture_timer75.fill(0);
                        drive_persistent.retained63 = drive_persistent.retained67 = 0;
                        // Retain these owners' EA88, EA7A and low bytes,
                        // including drive position/speed and fire13.
                        start_phase = StartPhase::state0;
                    }
                    if (!input_result.continue_frame) {
                        // Reset and stopped gates finish this foreground pass;
                        // the pending $17 byte survives until an actual consumer.
                        output_audio(audio.finish_frame());
                        if (!input_replay) trace_input(sampled_key);
                        continue;
                    }
                    if (start_phase == StartPhase::state0) {
                        dialog.emplace(payload, reset_state.characters, start_key, &replay_session.franchise);
                        input_scan.key17 = 0; // State0 initializes input and takes $8D86.
                        start_phase = StartPhase::running;
                    } else if (ending || (zuul && zuul->raw_state() > 44) ||
                        (city_controls && city_controls->transition() == ghostbusters::game::CityControlsTransition::insufficient_balance)) {
                        const auto result = tick_ending(input_scan.raw19, audio.irq_counter());
                        apply_ending_audio(result, audio);
                        city_key = 0;
                        if (result.restart_requested) {
                            if (!input_replay) return 2;
                            restart_after_frame = true;
                        }
                    } else if (zuul || (building_entry &&
                        building_entry->stage() == ghostbusters::game::BuildingEntry::Stage::ready &&
                        building_entry->transition() == ghostbusters::game::BuildingEntryTransition::zuul)) {
                        apply_zuul_audio(tick_zuul(joystick_input(), city_key, audio.irq_counter()));
                        city_key = 0;
                    } else if (marshmallow || (city_controls && city_controls->transition() == ghostbusters::game::CityControlsTransition::marshmallow)) {
                        tick_marshmallow(city_key);
                        city_key = 0;
                    } else if (capture_dispatch() == 32) {
                        tick_building_return(city_key);
                        city_key = 0;
                    } else if (capture_dispatch() == 28) {
                        const auto result = tick_catch(city_key);
                        city_key = 0;
                        for (const auto& event : result.audio_events) {
                            start_scene_speech(catch_sequence->state().city.characters, audio, event.command);
                        }
                    } else if (capture_dispatch() == 27) {
                        apply_beam_audio(tick_beam_controls(joystick_input(), city_key));
                        city_key = 0;
                    } else if (headquarters || (building_entry &&
                        building_entry->stage() == ghostbusters::game::BuildingEntry::Stage::ready &&
                        building_entry->transition() == ghostbusters::game::BuildingEntryTransition::ghostbusters_headquarters)) {
                        tick_headquarters(city_key);
                        city_key = 0;
                    } else if (building_controls ||
                        (building_entry && building_entry->stage() == ghostbusters::game::BuildingEntry::Stage::ready &&
                         building_entry->transition() == ghostbusters::game::BuildingEntryTransition::normal)) {
                        if (!building_controls) begin_building_controls();
                        tick_building_controls(joystick_input(), city_key);
                        city_key = 0;
                    } else if (building_entry ||
                        (drive_controls && drive_controls->transition() == ghostbusters::game::DriveControlsTransition::building) ||
                        (!drive_controls && city_controls && city_controls->transition() == ghostbusters::game::CityControlsTransition::building)) {
                        if (!building_entry) begin_building_entry();
                        tick_building_entry(city_key);
                        city_key = 0;
                    } else if (drive_controls || (drive_entry && drive_entry->stage() == ghostbusters::game::DriveEntry::Stage::ready)) {
                        if (!drive_controls) drive_controls.emplace(payload, *drive_entry, drive_persistent);
                        const auto events = tick_drive_controls(joystick_input(), city_key, audio.effect_busy());
                        for (const auto effect : events.started_effects)
                            if (effect == 0) audio.driving_capture_start();
                        city_key = 0;
                    } else if (drive_entry) {
                        if (update_common_city_frame(drive_entry->state(), city_key)) drive_entry->tick();
                        city_key = 0;
                    } else if (city_controls && city_controls->transition() == ghostbusters::game::CityControlsTransition::drive) {
                        drive_entry.emplace(payload, city_controls->state(), city->saved_vehicle_charset(), city->vehicle());
                        if (update_common_city_frame(drive_entry->state(), city_key)) drive_entry->tick();
                        city_key = 0;
                    } else if (city_controls) {
                        tick_city_controls(joystick_input(), city_key);
                        city_key = 0;
                    } else if (city) {
                        city->tick();
                        input_scan.key17 = 0; // States16/17 return through $8D86.
                        city_display = city->characters();
                        if (city->stage() == ghostbusters::game::CityEntry::Stage::ready)
                            city_controls.emplace(payload, *city, dialog->balance());
                        if (city->take_music_reset_request()) audio.enter_city();
                    } else if (equipment && equipment->stage() == ghostbusters::game::EquipmentSelection::Stage::finished) {
                        city.emplace(payload, *equipment);
                        city->tick();
                        input_scan.key17 = 0; // States16/17 return through $8D86.
                        city_display = city->characters();
                        if (city->take_music_reset_request()) audio.enter_city();
                    } else if (equipment) {
                        equipment->tick(audio.irq_counter(), {joystick_input(), equipment_key});
                        if (equipment->clears_input()) input_scan.key17 = 0;
                        equipment_key = 0;
                    }
                    else if (vehicle && vehicle->stage() == ghostbusters::game::VehicleSelection::Stage::purchased) {
                        equipment.emplace(payload, vehicle->characters(), vehicle->selected_vehicle(), vehicle->balance(), 0, 0);
                        input_scan.key17 = 0; // State14 -> $8D86.
                    }
                    else if (vehicle) {
                        vehicle->tick(audio.irq_counter(), ui_key);
                        if (vehicle->clears_input()) input_scan.key17 = 0;
                    }
                    else if (dialog->stage() == ghostbusters::game::FranchiseDialog::Stage::new_account_notice ||
                             dialog->stage() == ghostbusters::game::FranchiseDialog::Stage::account_accepted) {
                        vehicle.emplace(payload, dialog->characters(), dialog->balance());
                        input_scan.key17 = 0; // State6 -> $8D86.
                    } else {
                        dialog->tick(audio.irq_counter(), ui_key);
                        if (dialog->clears_input()) input_scan.key17 = 0;
                        // Script3's FF falls through to State6 in this frame.
                        if (dialog->stage() == ghostbusters::game::FranchiseDialog::Stage::new_account_notice) {
                            vehicle.emplace(payload, dialog->characters(), dialog->balance());
                            input_scan.key17 = 0; // State6 -> $8D86.
                        }
                    }
                    if (const auto* state = active_city_state()) input_scan.key17 = state->key17;
                    ui_key = 0;
                    apply_scene_audio_events();
                    output_audio(audio.finish_frame());
                    if (!input_replay) trace_input(sampled_key);
                }
                image = dialog_image();
                if (!display_enabled) image.fill(0); // $D011.DEN=0, border=$00.
            } else {
                for (unsigned i = 0; i < ticks; ++i) {
                    const bool space_down = input_replay
                        ? std::find(input_replay->keys().begin(), input_replay->keys().end(), SDLK_SPACE) != input_replay->keys().end()
                        : SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_SPACE];
                    const bool speech_before = audio.speech_active();
                    audio.begin_frame();
                    const auto title_event = title_sequence.tick(title.characters, space_down, speech_before);
                    title_ball.tick();
                    audio.synchronize_title_counter(title_sequence.counter());
                    if (title_event == ghostbusters::game::TitleSequenceEvent::start_speech_1)
                        audio.start_speech(1);
                    output_audio(audio.finish_frame());
                    if (title_startup_pending && !audio.speech_active()) {
                        title_startup_pending = false;
                        title_sequence.reset_timeline();
                        audio.synchronize_title_counter(title_sequence.counter());
                    }
                    if (!input_replay) trace_input(0);
                }
                scroller.write_row(payload, title.characters);
                image = ghostbusters::video::render_title(title.characters, scroller.fine_scroll(),
                                                          title_sequence.gate());
                ghostbusters::video::composite_sprites(image, title_ball.sprite_layer());
                image = with_footer(image, !audio.speech_active());
            }
            upload();
        }
        checked(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
        checked(SDL_RenderClear(renderer.get()));
        checked(SDL_RenderTexture(renderer.get(), texture.get(), nullptr, nullptr));
        checked(SDL_RenderPresent(renderer.get()));
        if (smoke) {
            audio_frame();
            break;
        }
        if (input_replay) {
            trace_input(replay_sampled_key);
            input_replay->advance();
            if (play_from_state && original_scene_state() == *play_from_state) {
                // Only relinquish the scripted keyboard. Scene, RNG, inventory
                // and SID state continue from the naturally played prefix.
                input_replay.reset();
                replay_session.lead_in_complete = true;
                audio_output.clear();
                clock = ghostbusters::game::PalClock{};
                last_time = SDL_GetTicksNS();
                checked(SDL_SetWindowSize(window.get(), 960, 720));
                checked(ghostbusters::platform::sdl::configure_presentation(renderer.get()));
                std::cout << "Live controls ready: " << options.play_from_name
                          << ". F11: fullscreen; Escape: exit.\n" << std::flush;
                continue;
            }
            if (input_replay->finished()) {
                if (play_from_state)
                    throw std::runtime_error("Input route ended before the requested scene");
                if (restart_after_frame) throw std::runtime_error(
                    "Input replay needs trailing frames to observe the requested restart");
                break;
            }
            if (restart_after_frame) {
                return 2;
            }
        } else SDL_Delay(4); // Display pacing is independent of the PAL logic clock.
    }
    return 0;
}

} // namespace ghostbusters::platform::sdl
