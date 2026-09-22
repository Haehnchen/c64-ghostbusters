# Release packaging for the current host.

include(CheckIPOSupported)
check_ipo_supported(RESULT _ghostbusters_ipo_supported)
if(_ghostbusters_ipo_supported)
    set_property(TARGET ghostbusters ghostbusters_core ghostbusters_audio
        ghostbusters_application ghostbusters_sdl_audio
        PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(WIN32)
    set_target_properties(ghostbusters PROPERTIES WIN32_EXECUTABLE TRUE)
    set(_ghostbusters_release_platform Windows)
elseif(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)
    endif()
    set_target_properties(ghostbusters PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "Ghostbusters"
        MACOSX_BUNDLE_BUNDLE_NAME "Ghostbusters"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.ghostbusters.game"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_INFO_PLIST
            "${PROJECT_SOURCE_DIR}/cmake/GhostbustersInfo.plist.in")
    set(_ghostbusters_release_platform macOS)
else()
    set(_ghostbusters_release_platform "${CMAKE_SYSTEM_NAME}")
endif()

set(_ghostbusters_release_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(_ghostbusters_release_arch MATCHES
   "^(AMD64|amd64|X64|x64|X86_64|x86_64)$")
    set(_ghostbusters_release_arch x86_64)
elseif(_ghostbusters_release_arch MATCHES
       "^(ARM64|arm64|AARCH64|aarch64)$")
    set(_ghostbusters_release_arch arm64)
endif()

set(GHOSTBUSTERS_RELEASE_DIR "${PROJECT_SOURCE_DIR}/build/release" CACHE PATH
    "Directory receiving the final Ghostbusters release ZIP")
set(GHOSTBUSTERS_SDL_NOTICE "" CACHE FILEPATH
    "SDL3 copyright or license file installed in release archives")
if(NOT GHOSTBUSTERS_SDL_NOTICE)
    # An empty cache entry suppresses find_file, so discard only that value.
    unset(GHOSTBUSTERS_SDL_NOTICE CACHE)
    find_file(GHOSTBUSTERS_SDL_NOTICE
        NAMES copyright LICENSE.txt LICENSE
        PATHS
            "/usr/share/doc/libsdl3-dev"
            "$ENV{RUNNER_TEMP}/SDL"
            "${SDL3_DIR}/../../.."
            "${SDL3_DIR}/../../../share/licenses/SDL3"
        NO_DEFAULT_PATH
        DOC "SDL3 copyright or license file installed in release archives")
endif()

set(_ghostbusters_sid_notice
    "${SID_PREFIX}/share/licenses/libresidfp/COPYING")
if(NOT EXISTS "${_ghostbusters_sid_notice}")
    message(FATAL_ERROR
        "Missing libresidfp license at ${_ghostbusters_sid_notice}. "
        "Run make audio-setup to refresh the pinned dependency.")
endif()
if(NOT GHOSTBUSTERS_SDL_NOTICE OR NOT EXISTS "${GHOSTBUSTERS_SDL_NOTICE}")
    message(FATAL_ERROR
        "SDL3 license was not found. Set GHOSTBUSTERS_SDL_NOTICE to the "
        "SDL3 LICENSE.txt or platform copyright file.")
endif()

set(_ghostbusters_release_stage
    "${CMAKE_BINARY_DIR}/_ghostbusters_release_stage")
set(_ghostbusters_release_readme
    "${CMAKE_BINARY_DIR}/ghostbusters-release-README.txt")
set(_ghostbusters_release_name
    "ghostbusters-${_ghostbusters_release_platform}-${_ghostbusters_release_arch}")
set(_ghostbusters_release_archive
    "${_ghostbusters_release_stage}/${_ghostbusters_release_name}.zip")

if(WIN32)
    set(_ghostbusters_release_readme_content
"Ghostbusters
Windows release.

Run ghostbusters.exe from this folder.

F1 starts a game; F11 toggles fullscreen; Escape exits.

The bundled archive includes the non-system runtime DLLs. Your graphics and audio drivers remain required.

")
elseif(APPLE)
    set(_ghostbusters_release_readme_content
"Ghostbusters
macOS release.

Open Ghostbusters.app in Finder, or run: open \"Ghostbusters.app\"

F1 starts a game; F11 toggles fullscreen; Escape exits.

This build is ad-hoc signed for local use and is not notarized. macOS may ask you to approve it on first launch.

")
else()
    set(_ghostbusters_release_readme_content
"Ghostbusters
Native port of the Commodore 64 game.

Run ./ghostbusters on the host system. This archive targets ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}; SDL3 is statically linked, while host operating-system libraries remain required.

F1 starts a game; F11 toggles fullscreen; Escape exits.

")
endif()

file(GENERATE OUTPUT "${_ghostbusters_release_readme}" CONTENT
    "${_ghostbusters_release_readme_content}")

if(APPLE)
    install(TARGETS ghostbusters BUNDLE DESTINATION .)
else()
    install(TARGETS ghostbusters RUNTIME DESTINATION .)
endif()
install(FILES "${_ghostbusters_release_readme}" DESTINATION .
    RENAME README.txt)
install(FILES "${_ghostbusters_sid_notice}" DESTINATION .
    RENAME COPYING.libresidfp)
install(FILES "${GHOSTBUSTERS_SDL_NOTICE}" DESTINATION .
    RENAME COPYING.SDL3)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME ghostbusters)
    set(CPACK_PACKAGE_FILE_NAME "${_ghostbusters_release_name}")
    set(CPACK_PACKAGE_DIRECTORY "${_ghostbusters_release_stage}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_STRIP_FILES TRUE)
    include(CPack)
endif()

set(_ghostbusters_bundle_commands)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    list(APPEND _ghostbusters_bundle_commands
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/package_linux.py"
            "$<TARGET_FILE:ghostbusters>"
            "${GHOSTBUSTERS_RELEASE_DIR}/${_ghostbusters_release_name}-bundled.zip"
            --strip "${CMAKE_STRIP}"
            --notice "${_ghostbusters_sid_notice}" COPYING.libresidfp
            --notice "${GHOSTBUSTERS_SDL_NOTICE}" COPYING.SDL3)
elseif(WIN32)
    set(_ghostbusters_windows_stage
        "${CMAKE_BINARY_DIR}/_ghostbusters_windows_stage")
    get_filename_component(_ghostbusters_windows_runtime_dir
        "${CMAKE_CXX_COMPILER}" DIRECTORY)
    list(APPEND _ghostbusters_bundle_commands
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
                "${_ghostbusters_windows_stage}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${_ghostbusters_windows_stage}"
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
                --prefix "${_ghostbusters_windows_stage}"
        COMMAND "${CMAKE_COMMAND}"
            "-DGHOSTBUSTERS_EXECUTABLE=${_ghostbusters_windows_stage}/ghostbusters.exe"
            "-DGHOSTBUSTERS_STAGE=${_ghostbusters_windows_stage}"
            "-DGHOSTBUSTERS_OUTPUT=${GHOSTBUSTERS_RELEASE_DIR}/${_ghostbusters_release_name}-bundled.zip"
            "-DGHOSTBUSTERS_STRIP=${CMAKE_STRIP}"
            "-DGHOSTBUSTERS_OBJDUMP=${CMAKE_OBJDUMP}"
            "-DGHOSTBUSTERS_RUNTIME_DIR=${_ghostbusters_windows_runtime_dir}"
            -P "${PROJECT_SOURCE_DIR}/cmake/BundleWindows.cmake")
elseif(APPLE)
    set(_ghostbusters_macos_stage
        "${CMAKE_BINARY_DIR}/_ghostbusters_macos_stage")
    list(APPEND _ghostbusters_bundle_commands
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
                "${_ghostbusters_macos_stage}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${_ghostbusters_macos_stage}"
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
                --prefix "${_ghostbusters_macos_stage}"
        COMMAND "${CMAKE_COMMAND}"
            "-DGHOSTBUSTERS_APP=${_ghostbusters_macos_stage}/Ghostbusters.app"
            "-DGHOSTBUSTERS_STAGE=${_ghostbusters_macos_stage}"
            "-DGHOSTBUSTERS_OUTPUT=${GHOSTBUSTERS_RELEASE_DIR}/${_ghostbusters_release_name}-bundled.zip"
            "-DGHOSTBUSTERS_STRIP=${CMAKE_STRIP}"
            -P "${PROJECT_SOURCE_DIR}/cmake/BundleMacOS.cmake")
endif()

set(_ghostbusters_release_commands
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${_ghostbusters_release_stage}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${GHOSTBUSTERS_RELEASE_DIR}")
set(_ghostbusters_release_byproducts)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _ghostbusters_release_commands
        COMMAND "${CMAKE_CPACK_COMMAND}" --config
                "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_ghostbusters_release_archive}"
                "${GHOSTBUSTERS_RELEASE_DIR}/${_ghostbusters_release_name}.zip")
    list(APPEND _ghostbusters_release_byproducts
        "${GHOSTBUSTERS_RELEASE_DIR}/${_ghostbusters_release_name}.zip")
endif()
if(WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _ghostbusters_release_byproducts
        "${GHOSTBUSTERS_RELEASE_DIR}/${_ghostbusters_release_name}-bundled.zip")
endif()

add_custom_target(release
    ${_ghostbusters_release_commands}
    ${_ghostbusters_bundle_commands}
    DEPENDS ghostbusters
    BYPRODUCTS ${_ghostbusters_release_byproducts}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    VERBATIM)
