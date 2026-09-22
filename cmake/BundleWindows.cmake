cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

if(DEFINED GHOSTBUSTERS_OBJDUMP AND NOT "${GHOSTBUSTERS_OBJDUMP}" STREQUAL ""
   AND NOT GHOSTBUSTERS_OBJDUMP MATCHES "-NOTFOUND$")
    set(CMAKE_OBJDUMP "${GHOSTBUSTERS_OBJDUMP}")
endif()
# This release uses MinGW's PE parser even when a Windows SDK is installed.
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
if(DEFINED GHOSTBUSTERS_OBJDUMP AND NOT "${GHOSTBUSTERS_OBJDUMP}" STREQUAL ""
   AND NOT GHOSTBUSTERS_OBJDUMP MATCHES "-NOTFOUND$")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${GHOSTBUSTERS_OBJDUMP}")
endif()

foreach(_ghostbusters_required
        GHOSTBUSTERS_EXECUTABLE GHOSTBUSTERS_STAGE GHOSTBUSTERS_OUTPUT
        GHOSTBUSTERS_RUNTIME_DIR)
    if(NOT DEFINED ${_ghostbusters_required} OR
       "${${_ghostbusters_required}}" STREQUAL "")
        message(FATAL_ERROR "${_ghostbusters_required} is required")
    endif()
endforeach()

if(NOT EXISTS "${GHOSTBUSTERS_EXECUTABLE}")
    message(FATAL_ERROR
        "Windows executable does not exist: ${GHOSTBUSTERS_EXECUTABLE}")
endif()

file(MAKE_DIRECTORY "${GHOSTBUSTERS_STAGE}")

if(DEFINED GHOSTBUSTERS_STRIP AND NOT "${GHOSTBUSTERS_STRIP}" STREQUAL ""
   AND NOT GHOSTBUSTERS_STRIP MATCHES "-NOTFOUND$")
    execute_process(
        COMMAND "${GHOSTBUSTERS_STRIP}" "${GHOSTBUSTERS_EXECUTABLE}"
        RESULT_VARIABLE _ghostbusters_strip_result
        ERROR_VARIABLE _ghostbusters_strip_error)
    if(NOT _ghostbusters_strip_result EQUAL 0)
        message(FATAL_ERROR
            "Could not strip Windows executable: ${_ghostbusters_strip_error}")
    endif()
endif()

# Keep Windows system DLLs and API-set contracts on the host. Everything else
# found by the dependency walker is copied beside ghostbusters.exe.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${GHOSTBUSTERS_EXECUTABLE}"
    DIRECTORIES "${GHOSTBUSTERS_RUNTIME_DIR}"
    RESOLVED_DEPENDENCIES_VAR _ghostbusters_resolved
    UNRESOLVED_DEPENDENCIES_VAR _ghostbusters_unresolved
    PRE_EXCLUDE_REGEXES
        "^[Aa][Pp][Ii]-[Mm][Ss]-[Ww][Ii][Nn]-.*"
        "^[Ee][Xx][Tt]-[Mm][Ss]-[Ww][Ii][Nn]-.*"
    POST_EXCLUDE_REGEXES
        ".*[\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\/].*"
        ".*[\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\/].*")

if(_ghostbusters_unresolved)
    string(JOIN ", " _ghostbusters_missing ${_ghostbusters_unresolved})
    message(FATAL_ERROR
        "Unresolved non-system Windows runtime dependencies: ${_ghostbusters_missing}")
endif()

if(NOT EXISTS "${GHOSTBUSTERS_STAGE}/README.txt")
    message(FATAL_ERROR "Windows release stage has no README.txt")
endif()
if(NOT EXISTS "${GHOSTBUSTERS_STAGE}/COPYING.libresidfp")
    message(FATAL_ERROR "Windows release stage has no libresidfp license")
endif()

foreach(_ghostbusters_dependency IN LISTS _ghostbusters_resolved)
    file(COPY "${_ghostbusters_dependency}" DESTINATION "${GHOSTBUSTERS_STAGE}")
endforeach()

file(GLOB _ghostbusters_archive_entries RELATIVE "${GHOSTBUSTERS_STAGE}"
    "${GHOSTBUSTERS_STAGE}/*")
if(NOT _ghostbusters_archive_entries)
    message(FATAL_ERROR "Windows release stage is empty")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${GHOSTBUSTERS_OUTPUT}"
            --format=zip ${_ghostbusters_archive_entries}
    WORKING_DIRECTORY "${GHOSTBUSTERS_STAGE}"
    RESULT_VARIABLE _ghostbusters_archive_result
    ERROR_VARIABLE _ghostbusters_archive_error)
if(NOT _ghostbusters_archive_result EQUAL 0)
    message(FATAL_ERROR
        "Could not create Windows ZIP: ${_ghostbusters_archive_error}")
endif()
