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
set(BUILD_NUM "${CMAKE_MATCH_3}")
set(CURRENT_VER "v${MASTER_VER}.${SUB_VER}.${BUILD_NUM}")

get_filename_component(SRC_DIR "${VERSION_FILE}" DIRECTORY)

file(WRITE "${SRC_DIR}/BuildInfo.h"
    "#pragma once\n"
    "\n"
    "// Authoritative build identity - update Version.id before release builds.\n"
    "// Format: v<BUILD_VERSION>.<BUILD_SUBVERSION>.<BUILD_NUMBER>\n"
    "constexpr int CURRENT_BUILD_VERSION    = ${MASTER_VER};\n"
    "constexpr int CURRENT_BUILD_SUBVERSION = ${SUB_VER};\n"
    "constexpr int CURRENT_BUILD            = ${BUILD_NUM};\n"
)

set(RELEASE_MD "${SRC_DIR}/ReleaseInfo.md")
if(EXISTS "${RELEASE_MD}")
    file(READ "${RELEASE_MD}" MD_CONTENT)
    string(REGEX REPLACE
        "\\*Current Build Version: v[0-9]+\\.[0-9]+\\.[0-9]+\\*"
        "*Current Build Version: ${CURRENT_VER}*"
        MD_CONTENT "${MD_CONTENT}"
    )
    file(WRITE "${RELEASE_MD}" "${MD_CONTENT}")
endif()

message(STATUS "----------------------------------------")
message(STATUS "VERSION SYNC: ${CURRENT_VER}")
message(STATUS "  Version.id:     authoritative")
message(STATUS "  BuildInfo.h:    CURRENT_BUILD = ${BUILD_NUM}")
message(STATUS "  ReleaseInfo.md: synced when present")
message(STATUS "----------------------------------------")