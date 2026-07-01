if("$ENV{SKIP_VERSION_INCREMENT}" STREQUAL "1")
    message(STATUS "Version increment skipped -- already handled by the build wrapper.")
    return()
endif()

if(NOT DEFINED VERSION_FILE OR VERSION_FILE STREQUAL "")
    message(FATAL_ERROR "VERSION_FILE was not provided")
endif()

if(NOT EXISTS "${VERSION_FILE}")
    message(FATAL_ERROR "Version.id not found at: ${VERSION_FILE}")
endif()

file(READ "${VERSION_FILE}" VER_CONTENT)
string(REGEX MATCH "v([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${VER_CONTENT}")

if(NOT CMAKE_MATCH_0)
    message(FATAL_ERROR "Could not parse version from Version.id")
endif()

set(MASTER_VER "${CMAKE_MATCH_1}")
set(SUB_VER "${CMAKE_MATCH_2}")
set(CUR_BUILD "${CMAKE_MATCH_3}")
math(EXPR NEW_BUILD "${CUR_BUILD} + 1")
set(NEW_VER "v${MASTER_VER}.${SUB_VER}.${NEW_BUILD}")

get_filename_component(SRC_DIR "${VERSION_FILE}" DIRECTORY)

file(WRITE "${VERSION_FILE}" "Current Build Version: ${NEW_VER}")

file(WRITE "${SRC_DIR}/BuildInfo.h"
    "#pragma once\n"
    "\n"
    "// Authoritative build identity - update Version.id before release builds.\n"
    "// Format: v<BUILD_VERSION>.<BUILD_SUBVERSION>.<BUILD_NUMBER>\n"
    "constexpr int CURRENT_BUILD_VERSION    = ${MASTER_VER};\n"
    "constexpr int CURRENT_BUILD_SUBVERSION = ${SUB_VER};\n"
    "constexpr int CURRENT_BUILD            = ${NEW_BUILD};\n"
)

set(RELEASE_MD "${SRC_DIR}/ReleaseInfo.md")
if(EXISTS "${RELEASE_MD}")
    file(READ "${RELEASE_MD}" MD_CONTENT)
    string(REGEX REPLACE
        "\\*Current Build Version: v[0-9]+\\.[0-9]+\\.[0-9]+\\*"
        "*Current Build Version: ${NEW_VER}*"
        MD_CONTENT "${MD_CONTENT}"
    )
    file(WRITE "${RELEASE_MD}" "${MD_CONTENT}")
endif()

message(STATUS "----------------------------------------")
message(STATUS "VERSION UPDATE: ${NEW_VER}")
message(STATUS "Previous: v${MASTER_VER}.${SUB_VER}.${CUR_BUILD}")
message(STATUS "Current:  ${NEW_VER}")
message(STATUS "  BuildInfo.h:    CURRENT_BUILD = ${NEW_BUILD}")
message(STATUS "  ReleaseInfo.md: synced when present")
message(STATUS "----------------------------------------")