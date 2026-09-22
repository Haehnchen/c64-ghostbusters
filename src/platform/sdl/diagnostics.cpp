#include "platform/sdl/application.hpp"
#include "platform/sdl/scene_view.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace ghostbusters::platform::sdl {
namespace {
void write_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("Cannot write " + path.string());
}

void dump_title(const std::filesystem::path& directory,
                const ghostbusters::game::TitleScreen& title,
                const ghostbusters::video::IndexedImage& image)
{
    std::filesystem::create_directories(directory);
    write_bytes(directory / "title-screen.bin", title.characters.screen);
    write_bytes(directory / "title-charset.bin", title.characters.charset);
    write_bytes(directory / "title-colors.bin", title.characters.colors);
    write_bytes(directory / "title-scene.bin", title.scene_data);
    write_bytes(directory / "title-indices.bin", image);
    std::ofstream ppm(directory / "title.ppm", std::ios::binary);
    ppm << "P6\n320 200\n255\n";
    for (const auto index : image) {
        const auto color = palette[index];
        const std::array<char, 3> rgb{static_cast<char>(color >> 16),
                                     static_cast<char>(color >> 8), static_cast<char>(color)};
        ppm.write(rgb.data(), 3);
    }
    if (!ppm) throw std::runtime_error("Cannot write title image");
}

} // namespace

int Application::run_diagnostic()
{
    const auto& dump = options.diagnostic_directory;
    const std::string_view mode = options.diagnostic_mode;
    constexpr std::string_view fixture_name = "SPENGLER,EGON";
    diagnostic_name.emplace();
    std::copy(fixture_name.begin(), fixture_name.end(), diagnostic_name->begin());
    const bool dump_ending = mode.starts_with("--dump-ending-");
    const bool dump_ending_poor = mode == "--dump-ending-poor";
    const bool dump_zuul_success = mode == "--dump-zuul-success" ||
        mode == "--dump-ending-success";
    const bool dump_zuul = dump_ending || dump_zuul_success || mode == "--dump-zuul";
    const bool dump_marshmallow_bait = mode == "--dump-marshmallow-bait";
    const bool dump_marshmallow = dump_marshmallow_bait || mode == "--dump-marshmallow";
    const bool dump_headquarters = mode == "--dump-headquarters";
    const bool dump_catch_failure = mode == "--dump-catch-failure";
    const bool dump_catch = dump_catch_failure || mode == "--dump-catch";
    const bool dump_beams = dump_catch || mode == "--dump-beams";
    const bool dump_building_return = mode == "--dump-building-return";
    const bool dump_pink_visit = mode == "--dump-pink-visit";
    const bool dump_building_controls = dump_building_return || dump_beams || mode == "--dump-building-controls";
    const bool dump_building_entry = dump_zuul || dump_headquarters || dump_building_controls || mode == "--dump-building-entry";
    const bool dump_drive_controls = dump_building_entry || mode == "--dump-drive-controls";
    const bool dump_drive_entry = dump_drive_controls || mode == "--dump-drive-entry";
    const bool dump_city_controls = dump_pink_visit || dump_marshmallow || dump_drive_entry || mode == "--dump-city-controls";
    const bool dump_city = mode != "--dump-title";
    unsigned equipment_category = 1;
    if (dump_pink_visit) equipment_category = 0;
    // One caller-owned clock across UI scene transitions in offline replays.
    std::uint8_t dump_irq_counter = 0;
    const auto next_dump_irq = [&] {
        dump_irq_counter = ghostbusters::game::advance_pal_counter(dump_irq_counter);
        return dump_irq_counter;
    };
    if (dump_city) {
        vehicle.emplace(payload, title.characters, ghostbusters::game::AccountBalanceBytes{1, 0, 0});
        for (unsigned tick = 0; tick < 4096; ++tick) vehicle->tick(next_dump_irq());
        vehicle->key('1');
        vehicle->key(0x0D);
        for (unsigned tick = 0; tick < 3; ++tick) vehicle->tick(next_dump_irq());
        if (vehicle->stage() != ghostbusters::game::VehicleSelection::Stage::purchased)
            throw std::runtime_error("Equipment dump requires a completed vehicle purchase");
        equipment.emplace(payload, vehicle->characters(), vehicle->selected_vehicle(), vehicle->balance(),
                          0, static_cast<std::uint8_t>(equipment_category));
        for (unsigned tick = 0; tick < 128 && equipment->stage() == ghostbusters::game::EquipmentSelection::Stage::initial; ++tick) equipment->tick(next_dump_irq());
        title.characters = equipment->characters();
        image = equipment_image(*equipment);
        std::filesystem::create_directories(dump);
        if (dump_city) {
            struct Segment { unsigned ticks; std::uint8_t joystick; std::uint8_t key; };
            if (dump_pink_visit) {
                // Buy the detector through the same forklift inputs as a player,
                // then change to the trap category. No inventory/status patches.
                constexpr std::array<Segment, 8> detector{{
                    {1,0xFF,0},{1,0xEF,0},{40,0xFF,0},{1,0xF7,0},
                    {80,0xFF,0},{1,0xEF,0},{30,0xFF,0},{1,0xFF,'2'}}};
                for (const auto segment : detector)
                    for (unsigned tick = 0; tick < segment.ticks; ++tick)
                        equipment->tick(next_dump_irq(), {segment.joystick,
                            static_cast<std::uint8_t>(tick == 0 ? segment.key : 0)});
                for (unsigned tick = 0; tick < 128 && equipment->stage() == ghostbusters::game::EquipmentSelection::Stage::initial; ++tick)
                    equipment->tick(next_dump_irq());
                if ((equipment->owned_mask() & 1) == 0)
                    throw std::runtime_error("Pink visit requires a purchased detector");
            }
            constexpr std::array<Segment, 11> replay{{
                {1,0xFF,0},{1,0xFD,0},{40,0xFF,0},{1,0xFF,0},{1,0xEF,0},
                {40,0xFF,0},{1,0xF7,0},{80,0xFF,0},{1,0xEF,0},{30,0xFF,0},{1,0xFF,'E'}}};
            for (const auto segment : replay)
                for (unsigned tick = 0; tick < segment.ticks; ++tick)
                    equipment->tick(next_dump_irq(), {segment.joystick, static_cast<std::uint8_t>(tick == 0 ? segment.key : 0)});
            if (equipment->stage() != ghostbusters::game::EquipmentSelection::Stage::finished)
                throw std::runtime_error("City dump requires a trap purchase and shop exit");
            city.emplace(payload, *equipment);
            city->tick(); city->tick();
            title.characters = city->characters();
            if (dump_city_controls) {
                city_controls.emplace(payload, *city, ghostbusters::game::AccountBalanceBytes{1, 0, 0});
                if (dump_pink_visit) {
                    ghostbusters::audio::SceneAudio replay_audio(payload,
                        ghostbusters::audio::SceneAudio::StartPoint::title_music);
                    replay_audio.leave_title();
                    replay_audio.enter_city();
                    replay_audio.enable_sid_trace();
                    std::ofstream trace(dump / "pink-visit.jsonl");
                    std::ofstream inputs(dump / "pink-visit-inputs.txt");
                    write_bytes(dump / "initial-hauntings.bin", city_controls->state().building_status_c8);
                    const std::array<std::uint8_t, 3> initial_clock{random06, replay_audio.irq_counter(), frame09};
                    write_bytes(dump / "initial-clock.bin", initial_clock);
                    write_bytes(dump / "initial-screen.bin", city_controls->state().characters.screen);
                    write_bytes(dump / "initial-colors.bin", city_controls->state().characters.colors);
                    int selected = -1;
                    unsigned visits = 0, returns = 0, steps = 0;
                    bool previous_was_city = true;
                    std::vector<std::uint8_t> visited_states;
                    const auto current = [&]() -> const ghostbusters::game::CityControlsState& {
                        if (building_return) return building_return->state().city;
                        if (building_controls) return building_controls->state().city;
                        if (building_entry) return building_entry->state().city;
                        if (drive_controls) return drive_controls->state().city;
                        if (drive_entry) return drive_entry->state();
                        return city_controls->state();
                    };
                    const auto array_json = [&](const auto& values) {
                        trace << '[';
                        for (std::size_t i = 0; i < values.size(); ++i) {
                            if (i) trace << ',';
                            trace << unsigned(values[i]);
                        }
                        trace << ']';
                    };
                    while (steps < 12000U) {
                        const auto& before = current();
                        std::uint8_t joystick = 0xFF;
                        if (before.state3a == 18) {
                            if (!previous_was_city) ++returns;
                            if (selected < 0) {
                                for (unsigned i = 4; i < 20; ++i)
                                    if (i != 10 && i != 17 && (before.building_status_c8[i] & 0x0C) == 4) {
                                        selected = static_cast<int>(i);
                                        break;
                                    }
                            }
                            if (selected >= 0) {
                                const auto x = before.sprites.x[0], y = before.sprites.y[0];
                                const auto target_x = 0x2B + (selected & 3) * 0x1C;
                                const auto target_y = 0x42 + ((selected / 4) - 1) * 0x28;
                                if (y != target_y) {
                                    if (x != 0x1D) joystick = x > 0x1D ? 0xFB : 0xF7;
                                    else joystick = y > target_y ? 0xFE : 0xFD;
                                } else if (x != target_x) joystick = x > target_x ? 0xFB : 0xF7;
                                else if (before.fire_latch11 != 0) joystick = 0xED;
                            }
                        }
                        previous_was_city = before.state3a == 18;
                        const auto state_before = before.state3a;
                        const auto ghost_before = before.sprites.pointers[4];
                        random06 = ghostbusters::game::advance_game_random(random06);
                        replay_audio.begin_frame();
                        frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                        if (capture_dispatch() == 32) tick_building_return(0);
                        else if (building_controls || (building_entry && building_entry->stage() == ghostbusters::game::BuildingEntry::Stage::ready)) {
                            if (!building_controls) begin_building_controls();
                            tick_building_controls(joystick, 0);
                        } else if (building_entry || (drive_controls && drive_controls->transition() == ghostbusters::game::DriveControlsTransition::building) ||
                                   (!drive_controls && city_controls->transition() == ghostbusters::game::CityControlsTransition::building)) {
                            if (!building_entry) begin_building_entry();
                            tick_building_entry(0);
                        } else if (drive_controls || (drive_entry && drive_entry->stage() == ghostbusters::game::DriveEntry::Stage::ready)) {
                            if (!drive_controls) drive_controls.emplace(payload, *drive_entry, drive_persistent);
                            const auto events = tick_drive_controls(joystick, 0, replay_audio.effect_busy());
                            for (auto effect : events.started_effects)
                                if (effect == 0) replay_audio.driving_capture_start();
                        } else if (drive_entry) {
                            if (update_common_city_frame(drive_entry->state(), 0)) drive_entry->tick();
                        } else if (city_controls->transition() == ghostbusters::game::CityControlsTransition::drive) {
                            drive_entry.emplace(payload, city_controls->state(), city->saved_vehicle_charset(), city->vehicle());
                            if (update_common_city_frame(drive_entry->state(), 0)) drive_entry->tick();
                        } else tick_city_controls(joystick, 0);
                        (void)replay_audio.finish_frame();
                        const auto& after = current();
                        if (visited_states.empty() || visited_states.back() != after.state3a)
                            visited_states.push_back(after.state3a);
                        if (state_before == 23 && after.state3a == 24) ++visits;
                        if (after.state3a == 24 && (after.current_building6e != selected ||
                            (after.building_status_c8.at(selected) & 0x0F) != 5))
                            throw std::runtime_error("Pink visit entered a different building or haunting phase");
                        if (state_before == 24 && after.state3a == 24 && after.sprites.pointers[4] != 0)
                            throw std::runtime_error("Early haunting retained a capture ghost after placement update");
                        trace << "{\"tick\":" << steps << ",\"joystick\":" << unsigned(joystick)
                              << ",\"state\":" << unsigned(after.state3a) << ",\"random06\":" << unsigned(random06)
                              << ",\"counter08\":" << unsigned(replay_audio.irq_counter()) << ",\"frame09\":" << unsigned(frame09)
                              << ",\"countdown7c\":" << unsigned(after.countdown7c) << ",\"building6e\":" << unsigned(after.current_building6e)
                              << ",\"route66\":" << unsigned(after.route_length66) << ",\"fire11\":" << unsigned(after.fire_latch11)
                              << ",\"fire13\":" << unsigned(drive_persistent.fire_latch13)
                              << ",\"direction64\":" << unsigned(drive_persistent.direction64)
                              << ",\"animation65\":" << unsigned(drive_persistent.animation65)
                              << ",\"enabled\":" << unsigned(after.sprites.enabled_mask) << ",\"statuses\":";
                        array_json(after.building_status_c8);
                        trace << ",\"pointers\":"; array_json(after.sprites.pointers);
                        trace << ",\"x\":"; array_json(after.sprites.x);
                        trace << ",\"y\":"; array_json(after.sprites.y);
                        trace << ",\"sid\":[";
                        const auto writes = replay_audio.take_sid_writes();
                        for (std::size_t i = 0; i < writes.size(); ++i) {
                            if (i) trace << ',';
                            trace << '[' << unsigned(writes[i].reg) << ',' << unsigned(writes[i].value) << ']';
                        }
                        trace << "]}\n";
                        inputs << unsigned(joystick) << '\n';
                        if (state_before != after.state3a || (after.state3a == 24 && ghost_before != 0 && after.sprites.pointers[4] == 0)) {
                            const auto checkpoint = dump / ("tick-" + std::to_string(steps));
                            std::filesystem::create_directories(checkpoint);
                            write_bytes(checkpoint / "screen.bin", after.characters.screen);
                            write_bytes(checkpoint / "colors.bin", after.characters.colors);
                        }
                        ++steps;
                        if (visits == 2 && returns == 1 && state_before == 24 && after.sprites.pointers[4] == 0) break;
                    }
                    if (steps == 12000 || visits != 2 || returns != 1 || !trace || !inputs)
                        throw std::runtime_error("Natural pink visit did not complete two entries and one return");
                    const std::vector<std::uint8_t> expected_states{18,19,20,21,22,23,24,32,33,17,18,22,23,24};
                    if (selected != 15 || visited_states != expected_states)
                        throw std::runtime_error("Pink visit did not follow the startup site's drive/return/revisit path");
                    title.characters = current().characters;
                }
                for (unsigned step = 0; !dump_pink_visit && step < 34; ++step) {
                    random06 = ghostbusters::game::advance_game_random(random06);
                    frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                    const std::uint8_t joystick = step == 0 || step == 33 ? 0xFF : step <= 16 ? 0xF7 : 0xFE;
                    tick_city_controls(joystick, 0);
                }
                if (!dump_pink_visit) title.characters = city_controls->state().characters;
                if (dump_marshmallow) {
                    // Matured-alert and bait fixtures on the real city replay.
                    auto& fixture = city_controls->state();
                    fixture.pending_alert80 = 5;
                    fixture.building_status_c8[5] = 0xF8;
                    fixture.bait69 = dump_marshmallow_bait ? 1 : 0;
                    city_controls->tick({random06, frame09, 0xFF, 0});
                    if (fixture.state3a != 36) throw std::runtime_error("Marshmallow replay did not enter State36");
                    const auto map_before = fixture.map_types_ea28[5];
                    ghostbusters::audio::SceneAudio scene_audio(payload,
                        ghostbusters::audio::SceneAudio::StartPoint::title_music);
                    scene_audio.leave_title();scene_audio.enter_city();scene_audio.voice3_effect_start(3);
                    unsigned steps = 0;
                    do {
                        random06 = ghostbusters::game::advance_game_random(random06);
                        scene_audio.begin_frame();
                        frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                        tick_marshmallow(dump_marshmallow_bait && steps == 0 ? 0x42 : 0);
                        if (marshmallow && marshmallow->state().countdown7c == 0xA0) {
                            auto snapshot = title;
                            snapshot.characters = marshmallow->state().characters;
                            dump_title(dump / "incident", snapshot, dialog_image());
                        }
                        (void)scene_audio.finish_frame();
                    } while (marshmallow && ++steps < 2048);
                    const auto& returned = city_controls->state();
                    const ghostbusters::game::AccountBalanceBytes expected{0, static_cast<std::uint8_t>(dump_marshmallow_bait ? 0x94 : 0x34), 0};
                    if (steps >= 2048 || returned.state3a != 18 || returned.balance57 != expected ||
                        returned.bait69 != 0 || returned.map_types_ea28[5] != (dump_marshmallow_bait ? map_before : 0))
                        throw std::runtime_error("Marshmallow replay failed money, bait, map or city return");
                    tick_city_controls(0xFF, 0);
                    title.characters = city_controls->state().characters;
                    const std::array<std::uint8_t, 4> summary{returned.state3a, returned.balance57.byte57,
                        returned.balance57.byte58, returned.balance57.byte59};
                    write_bytes(dump / "marshmallow-state.bin", summary);
                }
                if (dump_drive_entry) {
                    random06 = ghostbusters::game::advance_game_random(random06);
                    frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                    tick_city_controls(0xEF, 0);
                    if (city_controls->transition() != ghostbusters::game::CityControlsTransition::drive)
                        throw std::runtime_error("Drive preview requires a city fire selection");
                    drive_entry.emplace(payload, city_controls->state(), city->saved_vehicle_charset(), city->vehicle());
                    for (unsigned step = 0; step < 2; ++step) {
                        random06 = ghostbusters::game::advance_game_random(random06);
                        frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                        if (update_common_city_frame(drive_entry->state(), 0)) drive_entry->tick();
                    }
                    title.characters = drive_entry->characters();
                    if (dump_drive_controls) {
                        drive_controls.emplace(payload, *drive_entry, drive_persistent);
                        for (unsigned step = 0; step < 96; ++step) {
                            random06 = ghostbusters::game::advance_game_random(random06);
                            frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                            (void)tick_drive_controls(step < 48 ? 0xFB : step < 72 ? 0xF7 : 0xFF, 0, false);
                        }
                        title.characters = drive_controls->state().city.characters;
                        const auto& state = drive_controls->state();
                        if (state.city.state3a != 0x15 || state.vehicle_position63 != 76 || state.speed1b != 0x60)
                            throw std::runtime_error("Drive replay failed to steer and accelerate the vehicle");
                        const std::array<std::uint8_t, 7> summary{state.city.state3a,
                            state.vehicle_position63, state.speed1b, state.city.route_length66,
                            state.distance67, state.direction64, drive_d016};
                        write_bytes(dump / "drive-state.bin", summary);
                        if (dump_building_entry) {
                            for (unsigned step = 0; step < 8192 && drive_controls->transition() == ghostbusters::game::DriveControlsTransition::driving; ++step) {
                                random06 = ghostbusters::game::advance_game_random(random06);
                                frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                (void)tick_drive_controls(0xFF, 0, false);
                            }
                            if (drive_controls->transition() != ghostbusters::game::DriveControlsTransition::building)
                                throw std::runtime_error("Building replay did not finish the drive");
                            // HQ preview isolates building choice on the real completed drive.
                            if (dump_headquarters) drive_controls->state().city.current_building6e = 17;
                            if (dump_zuul) drive_controls->state().city.current_building6e = 10;
                            begin_building_entry();
                            for (unsigned step = 0; step < 2; ++step) {
                                random06 = ghostbusters::game::advance_game_random(random06);
                                frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                tick_building_entry(0);
                            }
                            if (building_entry->stage() != ghostbusters::game::BuildingEntry::Stage::ready)
                                throw std::runtime_error("Building setup did not complete");
                            title.characters = building_entry->characters();
                            const std::array<std::uint8_t, 3> building_state{
                                building_entry->state().city.state3a,
                                building_entry->state().city.current_building6e,
                                building_entry->state().vehicle5c};
                            write_bytes(dump / "building-state.bin", building_state);
                            if (dump_zuul) {
                                ghostbusters::audio::SceneAudio zuul_audio(payload,
                                    ghostbusters::audio::SceneAudio::StartPoint::title_music);
                                zuul_audio.leave_title();zuul_audio.enter_city();
                                unsigned steps=0, speech3=0, speech4=0;
                                bool rooftop_saved=false, ascent_saved=false;
                                auto speech=[&](const ghostbusters::game::ZuulTickResult& r){
                                    for(const auto& event:r.audio_events) {
                                        if(event.command==3)++speech3;
                                        if(event.command==4)++speech4;
                                        start_scene_speech(zuul->state().city.characters, zuul_audio, event.command);
                                    }
                                };
                                do {
                                    if(zuul && zuul->speech_blocked()) {
                                        collision_latch |= ghostbusters::video::sprite_collision_mask(scene_sprites(zuul->state().city.sprites));
                                        (void)zuul_audio.frame(false);
                                        if(!zuul_audio.speech_active()) {
                                            ghostbusters::game::end_gameplay_speech(zuul->state().city.characters);
                                            const auto r=zuul->resume_speech();consume_zuul_reads(r);speech(r);
                                        }
                                    } else {
                                        random06=ghostbusters::game::advance_game_random(random06);
                                        const bool restore_volume = !zuul || zuul->raw_state() < 42;
                                        zuul_audio.begin_frame(restore_volume);
                                        frame09=ghostbusters::game::advance_game_frame(frame09,0);
                                        std::uint8_t joystick=0xFF;
                                        if(zuul && zuul->raw_state()==41 && zuul->registers().gate_phase_ea77==0) {
                                            const auto x=zuul->state().city.sprites.target_x[5];
                                            joystick=x>0x5C ? 0xFB : x<0x5C ? 0xF7 : 0xFE;
                                            if(dump_zuul_success && x==0x5C &&
                                               zuul->state().city.sprites.target_y[5]==0xC0 &&
                                               (zuul_audio.irq_counter()&0x7F)!=6) joystick=0xFF;
                                        }
                                        speech(tick_zuul(joystick,0,zuul_audio.irq_counter()));
                                        if(!rooftop_saved && zuul->raw_state()==41 && zuul->registers().gate_phase_ea77==0) {
                                            rooftop_saved=true;
                                            auto snapshot=title;snapshot.characters=zuul->state().city.characters;
                                            dump_title(dump/"rooftop",snapshot,dialog_image());
                                        }
                                        if(!ascent_saved && zuul->transition()==ghostbusters::game::ZuulTransition::climb_setup) {
                                            ascent_saved=true;
                                            auto snapshot=title;snapshot.characters=zuul->state().city.characters;
                                            dump_title(dump/"ascent",snapshot,dialog_image());
                                        }
                                        (void)zuul_audio.finish_frame();
                                    }
                                } while(++steps<4096 && (zuul->raw_state()<=44 || zuul->speech_blocked()));
                                std::fprintf(stderr,"Zuul replay: state %u, steps %u, speech3 %u, speech4 %u\n",zuul->raw_state(),steps,speech3,speech4);
                                if(steps>=4096 || zuul->raw_state()!=(dump_zuul_success ? 54 : 46) ||
                                   speech3!=(dump_zuul_success ? 0U : 2U) || speech4!=(dump_zuul_success ? 1U : 0U))
                                    throw std::runtime_error("Zuul replay did not reach an ending handoff");
                                if(dump_zuul_success && zuul->state().city.balance57 != ghostbusters::game::AccountBalanceBytes{1,0x24,0})
                                    throw std::runtime_error("Zuul success replay lost its 5000-dollar reward");
                                title.characters=zuul->state().city.characters;
                                const std::array<std::uint8_t,4> summary{zuul->raw_state(),static_cast<std::uint8_t>(speech3),static_cast<std::uint8_t>(speech4),zuul->state().city.backup_men3d};
                                write_bytes(dump/"zuul-state.bin",summary);
                                if (dump_ending) {
                                    begin_ending();
                                    if (dump_ending_poor) {
                                        ending->state().city = city_controls->state();
                                        ending->state().city.state3a = 45;
                                        ending->state().city.balance57 = {0, 0x43, 0x21};
                                    }
                                    unsigned ending_steps = 0, ending_speech = 0;
                                    do {
                                        ghostbusters::game::EndingTickResult result;
                                        if (ending->speech_blocked()) {
                                            (void)zuul_audio.frame(false);
                                            if (!zuul_audio.speech_active()) {
                                                ghostbusters::game::end_gameplay_speech(ending->state().city.characters);
                                                result = ending->resume_speech();
                                            }
                                        } else {
                                            random06 = ghostbusters::game::advance_game_random(random06);
                                            zuul_audio.begin_frame(false);
                                            frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                            result = tick_ending(0x40, zuul_audio.irq_counter());
                                            for (const auto& event : result.audio_events)
                                                if (event.action == ghostbusters::game::EndingAudioAction::speech_start) ++ending_speech;
                                            apply_ending_audio(result, zuul_audio);
                                            (void)zuul_audio.finish_frame();
                                        }
                                    } while (++ending_steps < 16384 &&
                                        (ending->raw_state() != 58 || ending->script_active() || ending->speech_blocked()));
                                    if (ending_steps >= 16384 || ending_speech != (dump_ending_poor ? 1U : 0U))
                                        throw std::runtime_error("Ending replay failed to reach restart wait with expected speech");
                                    if (ending->tick({0, 1}).restart_requested ||
                                        !ending->tick({0, 0x20}).restart_requested ||
                                        !ending->tick({0, 0x28}).restart_requested)
                                        throw std::runtime_error("Ending restart key contract failed");
                                    title.characters = ending->state().city.characters;
                                    write_bytes(dump/"ending-account.bin", ending->registers().encoded_account_eac7);
                                    const auto balance = ending->state().city.balance57;
                                    const std::array<std::uint8_t, 3> balance_bytes{balance.byte57, balance.byte58, balance.byte59};
                                    write_bytes(dump/"ending-balance.bin", balance_bytes);
                                    std::fprintf(stderr, "Ending replay: state %u, steps %u, speech3 %u\n",
                                        ending->raw_state(), ending_steps, ending_speech);
                                }
                            }
                            if (dump_headquarters) {
                                auto& depleted = building_entry->state().city;
                                depleted.empty_traps6b = 0;
                                depleted.backup_men3d = 0;
                                depleted.backpack_charge3e = 0;
                                catch_persistent.full_traps6c = 7;
                                const auto balance_before = depleted.balance57;
                                ghostbusters::audio::SceneAudio hq_audio(payload,
                                    ghostbusters::audio::SceneAudio::StartPoint::title_music);
                                hq_audio.leave_title();
                                hq_audio.enter_city();
                                hq_audio.voice3_effect_start(0);
                                unsigned steps = 0;
                                do {
                                    random06 = ghostbusters::game::advance_game_random(random06);
                                    hq_audio.begin_frame();
                                    frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                    tick_headquarters(0);
                                    if (steps == 48 && headquarters) {
                                        auto snapshot = title;
                                        snapshot.characters = headquarters->state().city.characters;
                                        dump_title(dump / "departure", snapshot, dialog_image());
                                    }
                                    (void)hq_audio.finish_frame();
                                } while ((headquarters || building_entry) && ++steps < 1024);
                                const auto& refilled = city_controls->state();
                                if (steps >= 1024 || refilled.state3a != 18 || refilled.empty_traps6b != city->traps6a() ||
                                    refilled.backup_men3d != 3 || refilled.backpack_charge3e != 0x99 ||
                                    catch_persistent.full_traps6c != 0 || refilled.balance57 != balance_before)
                                    throw std::runtime_error("HQ replay failed refill and return to city");
                                tick_city_controls(0xFF, 0);
                                title.characters = city_controls->state().characters;
                                const std::array<std::uint8_t, 5> returned{refilled.state3a, refilled.empty_traps6b,
                                    catch_persistent.full_traps6c, refilled.backup_men3d, refilled.backpack_charge3e};
                                write_bytes(dump / "headquarters-state.bin", returned);
                            }
                            if (dump_building_controls) {
                                begin_building_controls();
                                const auto initial_x = building_controls->state().city.sprites.x[5];
                                for (unsigned step = 0; step < 64; ++step) {
                                    random06 = ghostbusters::game::advance_game_random(random06);
                                    frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                    tick_building_controls(0xFB, 0);
                                }
                                const auto& placement = building_controls->state().city;
                                if (placement.state3a != 24 || placement.sprites.x[5] >= initial_x || placement.countdown7c != 63)
                                    throw std::runtime_error("Building placement replay did not move the first figure");
                                title.characters = placement.characters;
                                const std::array<std::uint8_t, 4> placement_state{placement.state3a,
                                    placement.sprites.x[5], placement.sprites.target_x[5], placement.countdown7c};
                                write_bytes(dump / "placement-state.bin", placement_state);
                                if (dump_beams) {
                                    // Haunted-building fixture on the real replay's preserved state.
                                    auto& fixture = building_controls->state().city;
                                    fixture.building_status_c8[fixture.current_building6e] = 0x0C;
                                    for (unsigned step = 0; step < 6; ++step) {
                                        random06 = ghostbusters::game::advance_game_random(random06);
                                        frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                        tick_building_controls(step % 2 == 1 ? 0xEF : 0xFF, 0);
                                    }
                                    if (fixture.state3a != 27)
                                        throw std::runtime_error("Beam preview placement did not reach State27");
                                    for (unsigned step = 0; step < 12; ++step) {
                                        random06 = ghostbusters::game::advance_game_random(random06);
                                        frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                        (void)tick_beam_controls(0xFF, 0);
                                    }
                                    const auto& beams = beam_controls->state().city;
                                    if (beams.state3a != 27 || beam_controls->pending_irqs() != 0)
                                        throw std::runtime_error("Beam preview left the active state unexpectedly");
                                    title.characters = beams.characters;
                                    const std::array<std::uint8_t, 3> beam_state{
                                        beams.state3a, beams.backpack_charge3e, beam_controls->registers().beam_phase79};
                                    write_bytes(dump / "beam-state.bin", beam_state);
                                    if (dump_catch) {
                                        (void)tick_beam_controls(0xEF, 0);
                                        // Explicit interception fixture; all subsequent stages run normally.
                                        auto& capture = beam_controls->state().city.sprites;
                                        capture.x[7] = capture.target_x[7] = 0x70;
                                        capture.y[7] = capture.target_y[7] = 0xB0;
                                        capture.x[4] = capture.target_x[4] = 0x70;
                                        capture.y[4] = capture.target_y[4] = 0x6E;
                                        building_persistent.direction7d = 0;
                                        beam_persistent.beam_scratch7a = dump_catch_failure ? 0x9C : 0x17;
                                        ghostbusters::audio::SceneAudio preview_audio(payload,
                                            ghostbusters::audio::SceneAudio::StartPoint::title_music);
                                        preview_audio.leave_title();
                                        preview_audio.enter_city();
                                        unsigned speeches = 0, steps = 0;
                                        bool recorded_speech_frame = false;
                                        std::vector<std::uint8_t> capture_states;
                                        std::ofstream capture_trace(dump / "catch-frames.csv");
                                        capture_trace << "step,frame,state,ghost_pointer,ghost_x,ghost_y,enabled,speech_blocked,traps,balance57,balance58,balance59\n";
                                        auto record_capture = [&](const auto& state) {
                                            if (catch_sequence && catch_sequence->speech_blocked() && !recorded_speech_frame) {
                                                const auto speech_image = dialog_image();
                                                if (std::any_of(speech_image.begin() + 192 * 320, speech_image.end(),
                                                        [](auto pixel) { return pixel != 0; }))
                                                    throw std::runtime_error("Credits remained visible during catch speech");
                                                auto snapshot = title;
                                                snapshot.characters = state.characters;
                                                dump_title(dump / "speech", snapshot, speech_image);
                                                recorded_speech_frame = true;
                                            }
                                            if (capture_states.empty() || capture_states.back() != state.state3a) {
                                                capture_states.push_back(state.state3a);
                                                if (state.state3a == 32 || state.state3a == 33) {
                                                    const auto return_image = dialog_image();
                                                    if (std::none_of(return_image.begin() + 192 * 320, return_image.end(),
                                                            [](auto pixel) { return pixel != 0; }))
                                                        throw std::runtime_error("Credits did not return after catch speech");
                                                    auto snapshot = title;
                                                    snapshot.characters = state.characters;
                                                    dump_title(dump / (state.state3a == 32 ? "return-to-trap" : "return-to-car"),
                                                        snapshot, return_image);
                                                }
                                            }
                                            if (!dump_catch_failure && (state.state3a == 32 || state.state3a == 33)) {
                                                if (state.sprites.pointers[4] != 0 ||
                                                    std::any_of(state.sprites.bitmap_data[4].begin(), state.sprites.bitmap_data[4].begin() + 63,
                                                        [](auto byte) { return byte != 0; }))
                                                    throw std::runtime_error("Captured ghost became visible during return");
                                            }
                                            capture_trace << steps << ',' << unsigned(frame09) << ',' << unsigned(state.state3a)
                                                << ',' << unsigned(state.sprites.pointers[4]) << ',' << unsigned(state.sprites.x[4])
                                                << ',' << unsigned(state.sprites.y[4]) << ',' << unsigned(state.sprites.enabled_mask)
                                                << ',' << (catch_sequence && catch_sequence->speech_blocked())
                                                << ',' << unsigned(state.empty_traps6b) << ',' << unsigned(state.balance57.byte57)
                                                << ',' << unsigned(state.balance57.byte58) << ',' << unsigned(state.balance57.byte59) << '\n';
                                        };
                                        const auto traps_before = beam_controls->state().city.empty_traps6b;
                                        const auto balance_before = beam_controls->state().city.balance57;
                                        while ((beam_controls || catch_sequence || building_return || building_controls) && steps++ < 2048) {
                                            record_capture(catch_sequence ? catch_sequence->state().city :
                                                building_return ? building_return->state().city : beam_controls->state().city);
                                            if (catch_sequence && catch_sequence->speech_blocked()) {
                                                (void)preview_audio.frame(false);
                                                if (!preview_audio.speech_active()) {
                                                    ghostbusters::game::end_gameplay_speech(catch_sequence->state().city.characters);
                                                    (void)catch_sequence->resume_speech();
                                                    sync_catch();
                                                }
                                                continue;
                                            }
                                            random06 = ghostbusters::game::advance_game_random(random06);
                                            preview_audio.begin_frame();
                                            frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                            if (capture_dispatch() == 32) {
                                                tick_building_return(0);
                                            } else {
                                                if (capture_dispatch() != 28)
                                                    throw std::runtime_error("Live capture dispatch restarted an obsolete scene after success");
                                                const auto result = tick_catch(0);
                                                // Escape fixture at the original left boundary after28->29.
                                                if (dump_catch_failure && catch_sequence->state().city.state3a == 29 && !catch_sequence->speech_blocked()) {
                                                    auto& escape = catch_sequence->state().city.sprites;
                                                    escape.x[4] = escape.target_x[4] = 0;
                                                }
                                                for (const auto& event : result.audio_events) {
                                                    if (event.command != (dump_catch_failure ? 2 : 1)) throw std::runtime_error("Catch replay requested unexpected speech");
                                                    ++speeches;
                                                    start_scene_speech(catch_sequence->state().city.characters, preview_audio, event.command);
                                                }
                                            }
                                            (void)preview_audio.finish_frame();
                                        }
                                        if (steps >= 2048 || speeches != 1 || city_controls->state().state3a != 18 ||
                                            city_controls->state().empty_traps6b != static_cast<std::uint8_t>(traps_before - (dump_catch_failure ? 0 : 1)) ||
                                            ((city_controls->state().balance57 == balance_before) != dump_catch_failure) ||
                                            city_controls->state().sprites.enabled_mask != 0xFF)
                                            throw std::runtime_error("Catch replay did not award capture and return to the city");
                                        if (building_entry || building_controls || beam_controls || catch_sequence ||
                                            building_return || drive_entry || drive_controls)
                                            throw std::runtime_error("Finished capture retained scene owners for the next mission");
                                        record_capture(city_controls->state());
                                        const std::vector<std::uint8_t> expected_states = dump_catch_failure
                                            ? std::vector<std::uint8_t>{28, 29, 17, 18}
                                            : std::vector<std::uint8_t>{28, 30, 31, 32, 33, 17, 18};
                                        if (capture_states != expected_states || !capture_trace)
                                            throw std::runtime_error("Capture/return state sequence differs from original dispatcher");
                                        write_bytes(dump / "catch-state-path.bin", capture_states);
                                        tick_city_controls(0xFF, 0);
                                        if (!dump_catch_failure) {
                                            // The one-trap replay has spent its only empty trap.
                                            // A new fire edge must use city inventory rules, never
                                            // revive the retired building/beam controller.
                                            tick_city_controls(0xEF, 0);
                                            if (city_controls->state().state3a != 18 ||
                                                city_controls->state().empty_traps6b != 0 || capture_dispatch() != 24)
                                                throw std::runtime_error("Next mission attempt reused the completed catch");
                                            const std::array<std::uint8_t, 2> attempt{
                                                city_controls->state().state3a, city_controls->state().empty_traps6b};
                                            write_bytes(dump / "next-attempt.bin", attempt);
                                            tick_city_controls(0xFF, 0);
                                        }
                                        title.characters = city_controls->state().characters;
                                        const std::array<std::uint8_t, 3> caught{18, static_cast<std::uint8_t>(speeches), city_controls->state().empty_traps6b};
                                        write_bytes(dump / "catch-state.bin", caught);
                                    }
                                }
                                if (dump_building_return) {
                                    unsigned steps = 0;
                                    while ((building_controls || building_return) && steps++ < 1024) {
                                        random06 = ghostbusters::game::advance_game_random(random06);
                                        frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                        if (building_return || building_controls->state().city.state3a == 32)
                                            tick_building_return(0);
                                        else tick_building_controls(0xFF, 0);
                                    }
                                    if (building_controls || building_return || city_controls->state().state3a != 18)
                                        throw std::runtime_error("Building return replay did not restore the city");
                                    // Verify the restored scene can actually accept another input tick.
                                    random06 = ghostbusters::game::advance_game_random(random06);
                                    frame09 = ghostbusters::game::advance_game_frame(frame09, 0);
                                    tick_city_controls(0xFF, 0);
                                    title.characters = city_controls->state().characters;
                                    const std::array<std::uint8_t, 3> returned{
                                        city_controls->state().state3a,
                                        static_cast<std::uint8_t>(steps & 255),
                                        static_cast<std::uint8_t>(steps >> 8)};
                                    write_bytes(dump / "return-state.bin", returned);
                                }
                            }
                        }
                    }
                }
            }
            image = scene_image(title.characters,
                ending ? ending->state().city.sprites : zuul ? zuul->state().city.sprites : beam_controls ? beam_controls->state().city.sprites : building_controls ? building_controls->state().city.sprites : building_entry ? building_entry->sprites() : drive_controls ? drive_controls->state().city.sprites : drive_entry ? drive_entry->sprites() : city_controls ? city_controls->state().sprites : city->sprites(), 0,
                (dump_drive_controls && !dump_building_entry) ? std::optional<std::uint8_t>(drive_d016) : std::nullopt, (dump_pink_visit || dump_building_entry) && !dump_building_return && !dump_catch && !dump_headquarters);
            write_bytes(dump / "saved-vehicle.bin", city->saved_vehicle_charset());
        }
        const auto& sprites = ending ? ending->state().city.sprites : zuul ? zuul->state().city.sprites : beam_controls ? beam_controls->state().city.sprites : building_controls ? building_controls->state().city.sprites : building_entry ? building_entry->sprites() : drive_controls ? drive_controls->state().city.sprites : drive_entry ? drive_entry->sprites() : city_controls ? city_controls->state().sprites
            : dump_city ? city->sprites() : equipment->sprites();
        write_bytes(dump / "sprite-pointers.bin", sprites.pointers);
        std::array<std::uint8_t, 32> positions{};
        for (unsigned i = 0; i < 8; ++i) {
            positions[i * 2] = sprites.x[i]; positions[i * 2 + 1] = sprites.y[i];
            positions[16 + i * 2] = sprites.target_x[i]; positions[17 + i * 2] = sprites.target_y[i];
        }
        write_bytes(dump / "sprite-positions.bin", positions);
        const std::array<std::uint8_t, 2> shared{sprites.shared_multicolor_1, sprites.shared_multicolor_2};
        write_bytes(dump / "shared-colors.bin", shared);
        for (unsigned i = 0; i < 8; ++i) write_bytes(dump / ("sprite-" + std::to_string(i) + ".bin"), sprites.bitmap_data[i]);
    }
    if (dump_city) image = with_footer(image);
    dump_title(dump, title, image);
    return 0;

}
} // namespace ghostbusters::platform::sdl
