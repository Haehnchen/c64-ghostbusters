cmake_minimum_required(VERSION 3.20)
option(GHOSTBUSTERS_RELEASE "Build release ZIPs with statically linked SDL" OFF)
if(CMAKE_HOST_APPLE AND GHOSTBUSTERS_RELEASE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "Minimum macOS version")
endif()
project(ghostbusters LANGUAGES CXX)
option(GHOSTBUSTERS_COMPILER_CACHE "Use ccache when available" ON)
# Windows executables can carry their shell icon as a PE resource. Keep the
# resource compiler optional so native non-Windows builds do not acquire a
# platform-specific toolchain requirement.
set(_ghostbusters_windows_icon
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/ghostbusters.ico")
set(_ghostbusters_windows_icon_source)
if(WIN32)
    # Also notice the asset when it is added after the first configure.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_ghostbusters_windows_icon}")
endif()
if(WIN32 AND EXISTS "${_ghostbusters_windows_icon}")
    include(CheckLanguage)
    check_language(RC)
    if(CMAKE_RC_COMPILER AND NOT CMAKE_RC_COMPILER MATCHES "-NOTFOUND$")
        enable_language(RC)
        file(TO_CMAKE_PATH
            "${_ghostbusters_windows_icon}"
            _ghostbusters_windows_icon_path)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GhostbustersIcon.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/GhostbustersIcon.rc" @ONLY)
        set(_ghostbusters_windows_icon_source
            "${CMAKE_CURRENT_BINARY_DIR}/GhostbustersIcon.rc")
        set_source_files_properties("${_ghostbusters_windows_icon_source}"
            PROPERTIES OBJECT_DEPENDS "${_ghostbusters_windows_icon}")
    else()
        message(STATUS
            "Windows resource compiler unavailable; building without executable icon")
    endif()
endif()
# Ninja handles unchanged inputs; ccache also reuses compilations after a clean
# build. Preserve explicit launchers (including an intentionally empty one).
if(GHOSTBUSTERS_COMPILER_CACHE AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(GHOSTBUSTERS_CCACHE_PROGRAM ccache)
    if(GHOSTBUSTERS_CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${GHOSTBUSTERS_CCACHE_PROGRAM}")
    endif()
endif()
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

find_package(SDL3 CONFIG REQUIRED)
find_package(Threads REQUIRED)
if(GHOSTBUSTERS_RELEASE)
    set(GHOSTBUSTERS_SDL_TARGET SDL3::SDL3-static)
else()
    set(GHOSTBUSTERS_SDL_TARGET SDL3::SDL3)
endif()
set(SID_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/libresidfp" CACHE PATH "Pinned SID library installation")
if(NOT EXISTS "${SID_PREFIX}/lib/libresidfp.a")
    message(FATAL_ERROR "Missing SID library. Run make audio-setup once before configuring.")
endif()
add_library(residfp STATIC IMPORTED)
set_target_properties(residfp PROPERTIES
    IMPORTED_LOCATION "${SID_PREFIX}/lib/libresidfp.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SID_PREFIX}/include"
    INTERFACE_LINK_LIBRARIES Threads::Threads)
add_library(ghostbusters_audio src/audio/sid_chip.cpp src/audio/music_player.cpp src/audio/scene_audio.cpp src/audio/effect_player.cpp src/audio/text_sound.cpp)
target_sources(ghostbusters_audio PRIVATE src/audio/speech_assets.cpp)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/speech_assets.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_speech_assets.py"
        --output "${CMAKE_CURRENT_BINARY_DIR}/generated/speech_assets.inc"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/speech/speech_assets.bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/speech/speech_assets.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_speech_assets.py"
    VERBATIM)
target_sources(ghostbusters_audio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/speech_assets.inc")
target_include_directories(ghostbusters_audio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(ghostbusters_audio PUBLIC src)
target_compile_features(ghostbusters_audio PUBLIC cxx_std_20)
target_link_libraries(ghostbusters_audio PRIVATE residfp)
add_library(ghostbusters_sdl_audio src/platform/sdl/audio_output.cpp)
target_include_directories(ghostbusters_sdl_audio PUBLIC src)
target_compile_features(ghostbusters_sdl_audio PUBLIC cxx_std_20)
target_link_libraries(ghostbusters_sdl_audio PUBLIC ${GHOSTBUSTERS_SDL_TARGET})

file(GLOB_RECURSE GAME_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/manifest.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/graphics/*" "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/text/*" "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/music/*" "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/effects/*")
set(GAME_ASSET_OUTPUTS "${CMAKE_CURRENT_BINARY_DIR}/generated/game_assets.cpp")
add_custom_command(OUTPUT ${GAME_ASSET_OUTPUTS}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_game_assets.py"
        --assets "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        --output "${CMAKE_CURRENT_BINARY_DIR}/generated/game_assets.cpp"
    DEPENDS ${GAME_ASSET_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_game_assets.py"
    VERBATIM)
add_library(ghostbusters_core
    "${CMAKE_CURRENT_BINARY_DIR}/generated/game_assets.cpp"
    src/assets/payload.cpp
    src/game/title_screen.cpp
    src/game/title_scroller.cpp
    src/game/title_ball.cpp
    src/game/title_sequence.cpp
    src/game/text_script.cpp
    src/game/name_entry.cpp
    src/game/account_codec.cpp
    src/game/account_number.cpp
    src/game/bcd_money.cpp
    src/game/money_text.cpp
    src/game/vehicle_selection.cpp
    src/game/equipment_selection.cpp
    src/game/city_status.cpp
    src/game/city_frame.cpp
    src/game/input_frame.cpp
    src/game/game_reset.cpp
    src/game/text_irq.cpp
    src/game/keyboard_scanner.cpp
    src/game/city_controls.cpp
    src/game/drive_controls.cpp
    src/game/drive_frame.cpp
    src/game/building_return.cpp
    src/game/speech_presentation.cpp
    src/game/ending.cpp
    src/game/zuul.cpp
    src/game/marshmallow.cpp
    src/game/headquarters.cpp
    src/game/catch_sequence.cpp
    src/game/beam_controls.cpp
    src/game/building_controls.cpp
    src/game/building_entry.cpp
    src/game/drive_entry.cpp
    src/game/city_entry.cpp
    src/game/notice_scroller.cpp
    src/game/vehicle_graphics.cpp
    src/game/franchise_dialog.cpp
    src/video/character_frame.cpp
    src/video/game_scene.cpp
    src/video/sprite_collision.cpp
    src/video/sprite_layer.cpp
    src/video/title_view.cpp)
target_include_directories(ghostbusters_core PUBLIC src)
target_include_directories(ghostbusters_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(ghostbusters_core PUBLIC cxx_std_20)
set_target_properties(ghostbusters_core PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(ghostbusters_audio PUBLIC ghostbusters_core)

add_library(ghostbusters_application
    src/platform/sdl/application.cpp
    src/platform/sdl/game_session.cpp
    src/platform/sdl/diagnostics.cpp
    src/platform/sdl/scene_view.cpp
    src/platform/sdl/launch_options.cpp)
target_link_libraries(ghostbusters_application PUBLIC ghostbusters_core ghostbusters_audio ghostbusters_sdl_audio ${GHOSTBUSTERS_SDL_TARGET})
add_executable(ghostbusters src/platform/sdl/main.cpp)
target_compile_features(ghostbusters PRIVATE cxx_std_20)
set_target_properties(ghostbusters PROPERTIES CXX_EXTENSIONS OFF)
if(_ghostbusters_windows_icon_source)
    target_sources(ghostbusters PRIVATE "${_ghostbusters_windows_icon_source}")
endif()
target_link_libraries(ghostbusters PRIVATE ghostbusters_application)
if(GHOSTBUSTERS_RELEASE)
    include(cmake/Release.cmake)
endif()
if(MSVC)
    target_compile_options(ghostbusters_application PRIVATE /W4)
    target_compile_options(ghostbusters PRIVATE /W4)
    target_compile_options(ghostbusters_core PRIVATE /W4)
else()
    target_compile_options(ghostbusters_application PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ghostbusters PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ghostbusters_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(launch_options_test tests/platform/sdl/launch_options_test.cpp)
    target_link_libraries(launch_options_test PRIVATE ghostbusters_application)
    add_test(NAME launch_options COMMAND launch_options_test)
    add_test(NAME natural_playthrough COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_playthrough_test.py"
        "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_start.inputs")
    set_tests_properties(natural_playthrough PROPERTIES TIMEOUT 300)
    add_test(NAME natural_campaign COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_campaign_test.py"
        "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_capture.inputs"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_campaign.inputs")
    set_tests_properties(natural_campaign PROPERTIES TIMEOUT 600)
    # The ordinary victory route is already replayed and checked by both the
    # high-performance and laser scenarios; do not register its prefix again.
    add_test(NAME natural_zuul_failure COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_zuul_test.py"
        "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_marshmallow.inputs"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_zuul_failure.inputs" failure)
    set_tests_properties(natural_zuul_failure PROPERTIES TIMEOUT 660)
    add_test(NAME natural_high_performance COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_high_performance_capture_test.py"
        "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_marshmallow.inputs"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_high_performance.inputs"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_high_performance_capture.inputs")
    set_tests_properties(natural_high_performance PROPERTIES TIMEOUT 780)
    add_test(NAME natural_equipment_bounds COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_equipment_bounds_test.py"
        "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_equipment_bounds.inputs")
    set_tests_properties(natural_equipment_bounds PROPERTIES TIMEOUT 360)
    add_test(NAME natural_equipment_use COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_equipment_use_test.py"
        "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_equipment_use.inputs")
    set_tests_properties(natural_equipment_use PROPERTIES TIMEOUT 1140)
    add_test(NAME natural_sensor COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_sensor_test.py"
        "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_sensor.inputs")
    set_tests_properties(natural_sensor PROPERTIES TIMEOUT 1020)
    add_test(NAME natural_laser COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_laser_test.py"
        "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_marshmallow.inputs"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_zuul.inputs" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_laser.inputs")
    set_tests_properties(natural_laser PROPERTIES TIMEOUT 780)
    foreach(outcome victory failure)
        add_test(NAME natural_zuul_mixed_${outcome} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_zuul_test.py"
            "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_marshmallow.inputs"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_zuul_mixed_${outcome}.inputs" mixed_${outcome})
        set_tests_properties(natural_zuul_mixed_${outcome} PROPERTIES TIMEOUT 660)
    endforeach()
    foreach(outcome attack bait)
        add_test(NAME natural_marshmallow_${outcome} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_marshmallow_test.py"
            "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_marshmallow.inputs" ${outcome})
        set_tests_properties(natural_marshmallow_${outcome} PROPERTIES TIMEOUT 540)
    endforeach()
    add_executable(city_data_test tests/assets/city_data_test.cpp)
    target_link_libraries(city_data_test PRIVATE ghostbusters_core)
    add_test(NAME city_data COMMAND city_data_test)
    # Campaign checks the full first capture trace before its continuation.
    add_test(NAME natural_missed_trap COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_outcomes_test.py"
        "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_missed_trap.inputs" missed_trap)
    set_tests_properties(natural_missed_trap PROPERTIES TIMEOUT 360)
    foreach(vehicle hearse wagon)
        add_test(NAME natural_${vehicle} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/natural_outcomes_test.py"
            "$<TARGET_FILE:ghostbusters>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/fixtures/natural_${vehicle}.inputs" capture ${vehicle})
        set_tests_properties(natural_${vehicle} PROPERTIES TIMEOUT 360)
    endforeach()
    add_executable(vehicle_data_test tests/assets/vehicle_data_test.cpp)
    add_executable(capture_data_test tests/assets/capture_data_test.cpp)
    target_link_libraries(capture_data_test PRIVATE ghostbusters_core)
    add_test(NAME capture_data COMMAND capture_data_test)
    target_link_libraries(vehicle_data_test PRIVATE ghostbusters_core)
    add_test(NAME vehicle_data COMMAND vehicle_data_test)
    add_executable(ui_text_data_test tests/assets/ui_text_data_test.cpp)
    target_link_libraries(ui_text_data_test PRIVATE ghostbusters_core)
    add_test(NAME ui_text_data COMMAND ui_text_data_test)
    add_executable(equipment_data_test tests/assets/equipment_data_test.cpp)
    target_link_libraries(equipment_data_test PRIVATE ghostbusters_core)
    add_test(NAME equipment_data COMMAND equipment_data_test)
    add_executable(embedded_assets_test tests/assets/embedded_assets_test.cpp)
    target_link_libraries(embedded_assets_test PRIVATE ghostbusters_core)
    add_test(NAME embedded_assets COMMAND embedded_assets_test)
    add_test(NAME asset_embedding COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/embed_game_assets_test.py")
    add_executable(speech_assets_test tests/audio/speech_assets_test.cpp)
    target_link_libraries(speech_assets_test PRIVATE ghostbusters_audio)
    add_test(NAME speech_assets COMMAND speech_assets_test)
    add_test(NAME speech_asset_integrity COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_speech_assets.py" --verify
        --output "${CMAKE_CURRENT_BINARY_DIR}/generated/speech_assets.inc")
    add_executable(input_dispatch_test tests/platform/sdl/input_dispatch_test.cpp)
    add_executable(native_reset_test tests/game/native_reset_test.cpp)
    target_link_libraries(native_reset_test PRIVATE ghostbusters_core)
    add_test(NAME native_reset COMMAND native_reset_test)
    target_link_libraries(input_dispatch_test PRIVATE ghostbusters_core SDL3::SDL3)
    add_test(NAME input_dispatch COMMAND input_dispatch_test)
    add_executable(keyboard_scanner_test tests/game/keyboard_scanner_test.cpp)
    target_link_libraries(keyboard_scanner_test PRIVATE ghostbusters_core)
    add_test(NAME keyboard_scanner COMMAND keyboard_scanner_test)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME title_playthrough COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/title_playthrough_test.py"
        "$<TARGET_FILE:ghostbusters>")
    set_tests_properties(title_playthrough PROPERTIES TIMEOUT 130)
    add_executable(building_controls_test tests/game/building_controls_test.cpp)
    target_link_libraries(building_controls_test PRIVATE ghostbusters_core)
    add_test(NAME building_controls COMMAND building_controls_test)
    add_executable(building_entry_test tests/game/building_entry_test.cpp)
    target_link_libraries(building_entry_test PRIVATE ghostbusters_core)
    add_test(NAME building_entry COMMAND building_entry_test)
    add_test(NAME catch_failure_replay COMMAND ghostbusters --dump-catch-failure "${CMAKE_CURRENT_BINARY_DIR}/catch-failure-replay")
    add_test(NAME marshmallow_replay COMMAND ghostbusters --dump-marshmallow "${CMAKE_CURRENT_BINARY_DIR}/marshmallow-replay")
    add_test(NAME marshmallow_bait_replay COMMAND ghostbusters --dump-marshmallow-bait "${CMAKE_CURRENT_BINARY_DIR}/marshmallow-bait-replay")
    add_test(NAME zuul_success_replay COMMAND ghostbusters --dump-zuul-success "${CMAKE_CURRENT_BINARY_DIR}/zuul-success-replay")
    add_test(NAME zuul_replay COMMAND ghostbusters --dump-zuul "${CMAKE_CURRENT_BINARY_DIR}/zuul-replay")
    add_test(NAME headquarters_replay COMMAND ghostbusters --dump-headquarters "${CMAKE_CURRENT_BINARY_DIR}/headquarters-replay")
    add_test(NAME catch_replay COMMAND ghostbusters --dump-catch "${CMAKE_CURRENT_BINARY_DIR}/catch-replay")
    add_test(NAME beam_replay COMMAND ghostbusters --dump-beams "${CMAKE_CURRENT_BINARY_DIR}/beam-replay")
    add_test(NAME building_return_replay COMMAND ghostbusters --dump-building-return "${CMAKE_CURRENT_BINARY_DIR}/building-return-replay")
    add_test(NAME pink_visit_replay COMMAND ghostbusters --dump-pink-visit "${CMAKE_CURRENT_BINARY_DIR}/pink-visit-replay")
    add_test(NAME building_controls_replay COMMAND ghostbusters --dump-building-controls "${CMAKE_CURRENT_BINARY_DIR}/building-controls-replay")
    add_test(NAME building_replay COMMAND ghostbusters --dump-building-entry "${CMAKE_CURRENT_BINARY_DIR}/building-replay")
    add_test(NAME drive_replay COMMAND ghostbusters --dump-drive-controls "${CMAKE_CURRENT_BINARY_DIR}/drive-replay")
    add_executable(drive_keyboard_test tests/game/drive_keyboard_test.cpp)
    target_link_libraries(drive_keyboard_test PRIVATE ghostbusters_audio)
    add_test(NAME drive_keyboard COMMAND drive_keyboard_test)
    add_executable(drive_controls_test tests/game/drive_controls_test.cpp)
    target_link_libraries(drive_controls_test PRIVATE ghostbusters_core)
    add_test(NAME drive_controls COMMAND drive_controls_test)
    add_executable(drive_frame_test tests/game/drive_frame_test.cpp)
    target_link_libraries(drive_frame_test PRIVATE ghostbusters_core)
    add_test(NAME drive_frame COMMAND drive_frame_test)
    add_executable(drive_entry_test tests/game/drive_entry_test.cpp)
    target_link_libraries(drive_entry_test PRIVATE ghostbusters_core)
    add_test(NAME drive_entry COMMAND drive_entry_test)
    add_executable(city_frame_test tests/game/city_frame_test.cpp)
    target_link_libraries(city_frame_test PRIVATE ghostbusters_core)
    add_test(NAME city_frame COMMAND city_frame_test)
    add_executable(city_controls_test tests/game/city_controls_test.cpp)
    add_executable(building_selection_test tests/game/building_selection_test.cpp)
    target_link_libraries(building_selection_test PRIVATE ghostbusters_core)
    add_test(NAME building_selection COMMAND building_selection_test)
    target_link_libraries(city_controls_test PRIVATE ghostbusters_core)
    add_test(NAME city_controls COMMAND city_controls_test)
    add_executable(city_status_test tests/game/city_status_test.cpp)
    target_link_libraries(city_status_test PRIVATE ghostbusters_core)
    add_test(NAME city_status COMMAND city_status_test)
    add_executable(notice_scroller_test tests/game/notice_scroller_test.cpp)
    target_link_libraries(notice_scroller_test PRIVATE ghostbusters_core)
    add_test(NAME notice_scroller COMMAND notice_scroller_test)
    add_executable(city_entry_test tests/game/city_entry_test.cpp)
    target_link_libraries(city_entry_test PRIVATE ghostbusters_core)
    add_test(NAME city_entry COMMAND city_entry_test)
    add_executable(ending_test tests/game/ending_test.cpp)
    target_link_libraries(ending_test PRIVATE ghostbusters_core)
    add_test(NAME ending COMMAND ending_test)
    foreach(path success failure poor)
        add_test(NAME ending_${path}_replay COMMAND ghostbusters --dump-ending-${path} "${CMAKE_CURRENT_BINARY_DIR}/ending-${path}-replay")
    endforeach()
    add_executable(zuul_test tests/game/zuul_test.cpp)
    target_link_libraries(zuul_test PRIVATE ghostbusters_core)
    add_test(NAME zuul COMMAND zuul_test)
    add_executable(zuul_data_test tests/assets/zuul_data_test.cpp)
    target_link_libraries(zuul_data_test PRIVATE ghostbusters_core)
    add_test(NAME zuul_data COMMAND zuul_data_test)
    add_executable(zuul_route_test tests/game/zuul_route_test.cpp)
    target_link_libraries(zuul_route_test PRIVATE ghostbusters_core)
    add_test(NAME zuul_route COMMAND zuul_route_test)
    add_executable(marshmallow_test tests/game/marshmallow_test.cpp)
    target_link_libraries(marshmallow_test PRIVATE ghostbusters_core)
    add_test(NAME marshmallow COMMAND marshmallow_test)
    add_executable(headquarters_test tests/game/headquarters_test.cpp)
    target_link_libraries(headquarters_test PRIVATE ghostbusters_core)
    add_test(NAME headquarters COMMAND headquarters_test)
    add_executable(catch_sequence_test tests/game/catch_sequence_test.cpp)
    target_link_libraries(catch_sequence_test PRIVATE ghostbusters_core)
    add_test(NAME catch_sequence COMMAND catch_sequence_test)
    add_executable(beam_audio_test tests/audio/beam_audio_test.cpp)
    target_link_libraries(beam_audio_test PRIVATE ghostbusters_audio)
    add_test(NAME beam_audio COMMAND beam_audio_test)
    add_executable(beam_controls_test tests/game/beam_controls_test.cpp)
    target_link_libraries(beam_controls_test PRIVATE ghostbusters_core)
    add_test(NAME beam_controls COMMAND beam_controls_test)
    add_executable(city_redraw_test tests/game/city_redraw_test.cpp)
    target_link_libraries(city_redraw_test PRIVATE ghostbusters_core)
    add_test(NAME city_redraw COMMAND city_redraw_test)
    add_executable(building_return_test tests/game/building_return_test.cpp)
    target_link_libraries(building_return_test PRIVATE ghostbusters_core)
    add_test(NAME building_return COMMAND building_return_test)
    add_executable(effect_player_test tests/audio/effect_player_test.cpp)
    target_link_libraries(effect_player_test PRIVATE ghostbusters_audio)
    add_test(NAME effect_player COMMAND effect_player_test)


    add_executable(equipment_selection_test tests/game/equipment_selection_test.cpp)
    target_link_libraries(equipment_selection_test PRIVATE ghostbusters_core)
    add_test(NAME equipment_selection COMMAND equipment_selection_test)
    add_executable(scene_audio_test tests/audio/scene_audio_test.cpp)
    target_link_libraries(scene_audio_test PRIVATE ghostbusters_audio)
    add_test(NAME scene_audio COMMAND scene_audio_test)
    add_executable(text_sound_test tests/audio/text_sound_test.cpp)
    target_link_libraries(text_sound_test PRIVATE ghostbusters_audio)
    add_test(NAME text_sound COMMAND text_sound_test)
    add_executable(music_player_test tests/audio/music_player_test.cpp)
    target_link_libraries(music_player_test PRIVATE ghostbusters_audio)
    add_test(NAME music_player COMMAND music_player_test)
    add_executable(sid_chip_test tests/audio/sid_chip_test.cpp)
    target_link_libraries(sid_chip_test PRIVATE ghostbusters_audio)
    add_test(NAME sid_chip COMMAND sid_chip_test)
    add_executable(audio_output_test tests/platform/sdl/audio_output_test.cpp)
    target_link_libraries(audio_output_test PRIVATE ghostbusters_sdl_audio)
    add_test(NAME audio_output COMMAND audio_output_test)
    set_tests_properties(audio_output PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
    add_executable(window_presentation_test tests/platform/sdl/window_presentation_test.cpp)
    target_link_libraries(window_presentation_test PRIVATE ghostbusters_core SDL3::SDL3)
    add_test(NAME window_presentation COMMAND window_presentation_test)
    set_tests_properties(window_presentation PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    foreach(test_name IN ITEMS input_frame game_scene sprite_collision character_frame sprite_layer title_screen title_scroller title_ball title_sequence title_view text_script name_entry account_number account_codec bcd_money money_text franchise_dialog vehicle_selection)
        if(test_name MATCHES "^(game_scene|sprite_collision|character_frame|sprite_layer|title_view)$")
            set(test_directory video)
        else()
            set(test_directory game)
        endif()
        add_executable(${test_name}_test tests/${test_directory}/${test_name}_test.cpp)
        target_link_libraries(${test_name}_test PRIVATE ghostbusters_core)
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()
    add_test(NAME sdl_smoke COMMAND ghostbusters --smoke-test)
    set_tests_properties(sdl_smoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software;SDL_AUDIODRIVER=dummy")

    add_test(NAME test_runner COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/run_tests_test.py")
    add_test(NAME audio_setup COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/setup_audio_test.py")
    add_test(NAME linux_packaging COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/package_linux_test.py")
    add_test(NAME play_from COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/play_from_test.py" "$<TARGET_FILE:ghostbusters>")
    add_test(NAME late_scene_schedule COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/play_late_scene_test.py")

    # Full natural paths synthesize minutes of audio. Keep their coverage, but
    # do not charge every edit for it.
    set(long_tests natural_playthrough natural_campaign
        natural_zuul_failure natural_high_performance natural_equipment_bounds
        natural_equipment_use natural_sensor natural_laser
        natural_zuul_mixed_victory natural_zuul_mixed_failure
        natural_marshmallow_attack natural_marshmallow_bait
        natural_missed_trap natural_hearse natural_wagon title_playthrough)
    # Four connected routes cover crossed streams, deployments/HQ/financial loss,
    # mixed gate recovery/victory/resume, and defeat/account reuse. Remaining
    # vehicle, equipment and outcome variants are opt-in via check-all or -R.
    set(extended_tests natural_playthrough natural_campaign natural_zuul_mixed_victory natural_zuul_failure)
    get_property(registered_tests DIRECTORY PROPERTY TESTS)
    foreach(test_name IN LISTS registered_tests)
        if(test_name IN_LIST extended_tests)
            set_tests_properties(${test_name} PROPERTIES LABELS extended)
        elseif(test_name IN_LIST long_tests)
            set_tests_properties(${test_name} PROPERTIES LABELS variants)
        else()
            set_tests_properties(${test_name} PROPERTIES LABELS quick TIMEOUT 30)
        endif()
    endforeach()
endif()
