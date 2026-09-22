#pragma once

#include <SDL3/SDL.h>

#include "assets/payload.hpp"
#include "assets/embedded.hpp"
#include "assets/city_data.hpp"
#include "assets/ui_data.hpp"
#include "audio/scene_audio.hpp"
#include "platform/sdl/audio_output.hpp"
#include "platform/sdl/window_presentation.hpp"
#include "game/title_screen.hpp"
#include "game/title_scroller.hpp"
#include "game/title_ball.hpp"
#include "game/title_sequence.hpp"
#include "game/pal_clock.hpp"
#include "game/pal_counter.hpp"
#include "game/franchise_dialog.hpp"
#include "game/vehicle_selection.hpp"
#include "game/equipment_selection.hpp"
#include "game/city_entry.hpp"
#include "game/city_status.hpp"
#include "game/city_controls.hpp"
#include "game/city_frame.hpp"
#include "game/input_frame.hpp"
#include "game/game_reset.hpp"
#include "game/keyboard_scanner.hpp"
#include "platform/sdl/input.hpp"
#include "platform/sdl/input_replay.hpp"
#include "game/drive_entry.hpp"
#include "game/building_entry.hpp"
#include "game/building_controls.hpp"
#include "game/building_return.hpp"
#include "game/beam_controls.hpp"
#include "game/catch_sequence.hpp"
#include "game/headquarters.hpp"
#include "game/marshmallow.hpp"
#include "game/zuul.hpp"
#include "game/ending.hpp"
#include "game/franchise_session.hpp"
#include "video/game_scene.hpp"
#include "game/speech_presentation.hpp"
#include "game/drive_controls.hpp"
#include "game/drive_frame.hpp"
#include "game/runtime_clock.hpp"
#include "game/notice_scroller.hpp"
#include "video/sprite_layer.hpp"
#include "video/sprite_collision.hpp"
#include "video/character_frame.hpp"
#include "video/title_view.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>


#include "platform/sdl/launch_options.hpp"

namespace ghostbusters::platform::sdl {

// Keep the franchise account, input schedule and diagnostics across restart.
// Scene, graphics and audio owners are reconstructed with each Application.
struct ReplaySession {
    ghostbusters::game::FranchiseSession franchise;
    std::optional<ghostbusters::platform::sdl::InputReplay> input;
    std::uint64_t frame = 0;
    unsigned generation = 0;
    std::uint64_t audio_samples = 0;
    std::uint64_t sid_writes = 0;
    double audio_energy = 0;
    bool fullscreen = false;
    bool lead_in_complete = false;
};


// Shared scene ownership and transitions for live play and scene regression runs.
// A restart constructs a fresh Application while ReplaySession retains the account
// and input route. Do not copy: scene objects hold references to owned asset views.
class Application {
public:
    Application(const LaunchOptions& launch, ReplaySession& replay)
        : options(launch), replay_session(replay) { reset_state.characters = title.characters; }
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    int run();

private:
    int run_diagnostic();
    ghostbusters::video::IndexedImage with_footer(ghostbusters::video::IndexedImage result, bool visible = true);
    void drive_prefix(ghostbusters::game::DriveControlsState& state);
    std::uint8_t joystick_input();
    std::optional<std::uint8_t> update_common_city_frame(ghostbusters::game::CityControlsState& state,
                                        std::uint8_t key, bool countdown_done = false, bool defer_after = false);
    void tick_city_controls(std::uint8_t joystick, std::uint8_t key);
    void tick_marshmallow(std::uint8_t key);
    ghostbusters::game::DriveControlsTickResult tick_drive_controls(std::uint8_t joystick, std::uint8_t key, bool effect_busy);
    void begin_building_entry();
    void tick_building_entry(std::uint8_t key);
    void begin_building_controls();
    void tick_building_controls(std::uint8_t joystick, std::uint8_t key);
    ghostbusters::game::BeamControlsTickResult tick_beam_controls(std::uint8_t joystick, std::uint8_t key);
    void sync_zuul();
    void consume_zuul_reads(const ghostbusters::game::ZuulTickResult& result);
    ghostbusters::game::ZuulTickResult tick_zuul(std::uint8_t joystick, std::uint8_t key, std::uint8_t irq_counter);
    void sync_catch();
    ghostbusters::game::CatchSequenceTickResult tick_catch(std::uint8_t key);
    int capture_dispatch();
    void restore_city(ghostbusters::game::DriveControlsState& state);
    void tick_headquarters(std::uint8_t key);
    void tick_building_return(std::uint8_t key);
    void begin_ending();
    ghostbusters::game::EndingTickResult tick_ending(std::uint8_t raw_key, std::uint8_t irq_counter);
    ghostbusters::game::DriveControlsState* active_drive_state();
    ghostbusters::game::CityControlsState* active_city_state();
    std::uint8_t original_scene_state();
    void apply_ending_audio(const ghostbusters::game::EndingTickResult& result,
                                   ghostbusters::audio::SceneAudio& player);
    static void start_scene_speech(ghostbusters::video::CharacterFrame& characters,
                                   ghostbusters::audio::SceneAudio& player,
                                   std::uint8_t command);
    ghostbusters::video::IndexedImage scene_content_image();
    ghostbusters::video::IndexedImage dialog_image();

    const LaunchOptions& options;
    ReplaySession& replay_session;
    std::optional<std::array<std::uint8_t, 20>> diagnostic_name;
    const ghostbusters::assets::Payload payload = ghostbusters::assets::Payload::embedded();
    const std::span<const std::uint8_t> font = ghostbusters::assets::embedded_font();
    ghostbusters::game::TitleScreen title = ghostbusters::game::prepare_title_screen(payload, font);
    ghostbusters::game::TitleScroller scroller;
    ghostbusters::game::TitleBall title_ball{payload, title.scene_data};
    ghostbusters::game::TitleSequence title_sequence{payload};
    ghostbusters::video::CharacterFrame footer_characters = title.characters;
    const ghostbusters::assets::UiData ui_data{payload};

    ghostbusters::video::IndexedImage image = ghostbusters::video::render_characters(title.characters);
    std::optional<ghostbusters::game::FranchiseDialog> dialog;
    std::optional<ghostbusters::game::VehicleSelection> vehicle;
    std::optional<ghostbusters::game::EquipmentSelection> equipment;
    std::optional<ghostbusters::game::CityEntry> city;
    std::optional<ghostbusters::video::CharacterFrame> city_display;
    std::optional<ghostbusters::game::CityControls> city_controls;
    std::optional<ghostbusters::game::DriveEntry> drive_entry;
    std::optional<ghostbusters::game::DriveControls> drive_controls;
    std::optional<ghostbusters::game::BuildingEntry> building_entry;
    std::optional<ghostbusters::game::BuildingControls> building_controls;
    std::optional<ghostbusters::game::BuildingReturn> building_return;
    std::optional<ghostbusters::game::BeamControls> beam_controls;
    std::optional<ghostbusters::game::CatchSequence> catch_sequence;
    std::optional<ghostbusters::game::Headquarters> headquarters;
    std::optional<ghostbusters::game::Marshmallow> marshmallow;
    std::optional<ghostbusters::game::Zuul> zuul;
    std::optional<ghostbusters::game::Ending> ending;
    std::uint8_t collision_latch = 0;
    ghostbusters::game::CatchSequenceRegisters catch_persistent{0, 0, 0};
    ghostbusters::game::BeamControlsRegisters beam_persistent{0, 0, 0};
    // Startup-clear state; these bytes persist across subsequent building visits.
    ghostbusters::game::BuildingControlsRegisters building_persistent{0, 0, {0, 0}, 0, {0, 0}};
    // Cleared at startup, retained between driving scenes.
    ghostbusters::game::DriveControlsPersistent drive_persistent{0, 0, 0, {0, 0}, {0, 0}};
    std::uint8_t drive_d016 = 0x17;
    // The game-mode audio tick advances random06; common frames advance frame09.
    ghostbusters::game::InputFrameState input_frame;
    ghostbusters::game::KeyboardScannerState input_scan;
    std::uint8_t random06 = 1, city_key = 0;
    std::uint8_t& frame09 = input_frame.frame09;
    std::uint8_t equipment_key = 0;
    std::uint8_t ui_key = 0;
    std::uint8_t& pause_flags47 = input_frame.flags47;
    std::uint8_t sampled_joystick = 0xFF;
    bool display_enabled = true;
    std::uint8_t foreground_d011 = 0x1B;
    bool live_prefix_done = false;
    // Reset and initial-state setup each own a separate foreground frame
    // after title exit, before the dialog's text can run.
    enum class StartPhase { running, reset_requested, state0 };
    StartPhase start_phase = StartPhase::running;
    ghostbusters::game::StartKey start_key = ghostbusters::game::StartKey::f1;
    ghostbusters::game::GameReset last_reset = ghostbusters::game::GameReset::none;
    ghostbusters::game::GameResetState reset_state;
};

} // namespace ghostbusters::platform::sdl
