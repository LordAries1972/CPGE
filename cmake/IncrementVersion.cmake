set(MASTER_VER 0)
set(SUB_VER 0)
set(DEFAULT_BUILD 695)

if(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" VER_CONTENT)
    string(REGEX MATCH "v[0-9]+\\.[0-9]+\\.([0-9]+)" _ "${VER_CONTENT}")
    set(CUR_BUILD "${CMAKE_MATCH_1}")
endif()

if(NOT CUR_BUILD OR CUR_BUILD STREQUAL "")
    set(CUR_BUILD ${DEFAULT_BUILD})
endif()

math(EXPR NEW_BUILD "${CUR_BUILD} + 1")
set(NEW_VER "v${MASTER_VER}.${SUB_VER}.${NEW_BUILD}")

file(WRITE "${VERSION_FILE}" "Current Build Version: ${NEW_VER}")
message(STATUS "════════════════════════════════════════")
message(STATUS "VERSION UPDATE: ${NEW_VER}")
message(STATUS "Previous: v${MASTER_VER}.${SUB_VER}.${CUR_BUILD}")
message(STATUS "Current:  ${NEW_VER}")
message(STATUS "════════════════════════════════════════")
