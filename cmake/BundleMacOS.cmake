cmake_minimum_required(VERSION 3.20)

foreach(_ghostbusters_required
        GHOSTBUSTERS_APP GHOSTBUSTERS_STAGE GHOSTBUSTERS_OUTPUT)
    if(NOT DEFINED ${_ghostbusters_required} OR
       "${${_ghostbusters_required}}" STREQUAL "")
        message(FATAL_ERROR "${_ghostbusters_required} is required")
    endif()
endforeach()

if(NOT EXISTS "${GHOSTBUSTERS_APP}")
    message(FATAL_ERROR
        "macOS app bundle does not exist: ${GHOSTBUSTERS_APP}")
endif()

include(BundleUtilities)
get_bundle_main_executable("${GHOSTBUSTERS_APP}" _ghostbusters_executable)
if(_ghostbusters_executable MATCHES "^error:")
    message(FATAL_ERROR
        "Could not find macOS bundle executable: ${_ghostbusters_executable}")
endif()

if(DEFINED GHOSTBUSTERS_STRIP AND NOT "${GHOSTBUSTERS_STRIP}" STREQUAL ""
   AND NOT GHOSTBUSTERS_STRIP MATCHES "-NOTFOUND$")
    execute_process(
        COMMAND "${GHOSTBUSTERS_STRIP}" -x "${_ghostbusters_executable}"
        RESULT_VARIABLE _ghostbusters_strip_result
        ERROR_VARIABLE _ghostbusters_strip_error)
    if(NOT _ghostbusters_strip_result EQUAL 0)
        message(FATAL_ERROR
            "Could not strip macOS executable: ${_ghostbusters_strip_error}")
    endif()
endif()

# fixup_bundle copies non-system dylibs into Contents/Frameworks and updates
# their install names. Apple frameworks and system libraries stay on the host.
fixup_bundle("${GHOSTBUSTERS_APP}" "" "${GHOSTBUSTERS_STAGE}")

find_program(_ghostbusters_codesign codesign)
if(NOT _ghostbusters_codesign)
    message(FATAL_ERROR "codesign is required to create the macOS release")
endif()
execute_process(
    COMMAND "${_ghostbusters_codesign}" --deep --force --sign -
            "${GHOSTBUSTERS_APP}"
    RESULT_VARIABLE _ghostbusters_codesign_result
    ERROR_VARIABLE _ghostbusters_codesign_error)
if(NOT _ghostbusters_codesign_result EQUAL 0)
    message(FATAL_ERROR
        "Could not ad-hoc sign macOS app: ${_ghostbusters_codesign_error}")
endif()

if(NOT EXISTS "${GHOSTBUSTERS_STAGE}/README.txt")
    message(FATAL_ERROR "macOS release stage has no README.txt")
endif()
if(NOT EXISTS "${GHOSTBUSTERS_STAGE}/COPYING.libresidfp")
    message(FATAL_ERROR "macOS release stage has no libresidfp license")
endif()

file(GLOB _ghostbusters_archive_entries RELATIVE "${GHOSTBUSTERS_STAGE}"
    "${GHOSTBUSTERS_STAGE}/*")
if(NOT _ghostbusters_archive_entries)
    message(FATAL_ERROR "macOS release stage is empty")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${GHOSTBUSTERS_OUTPUT}"
            --format=zip ${_ghostbusters_archive_entries}
    WORKING_DIRECTORY "${GHOSTBUSTERS_STAGE}"
    RESULT_VARIABLE _ghostbusters_archive_result
    ERROR_VARIABLE _ghostbusters_archive_error)
if(NOT _ghostbusters_archive_result EQUAL 0)
    message(FATAL_ERROR
        "Could not create macOS ZIP: ${_ghostbusters_archive_error}")
endif()
