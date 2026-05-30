# toolchain-android.cmake
#
# Optional thin wrapper around the Android NDK's own toolchain file.
# Use this if you want to configure NDK defaults in one place rather
# than passing them on every cmake invocation.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=Linux/toolchain-android.cmake \
#         -DANDROID_NDK=/path/to/ndk/<version>                  \
#         -DRENDERER=Vulkan                                       \
#         -S Linux -B build-android/arm64-v8a/Vulkan/Debug
#
# All variables below can be overridden from the cmake command line.

# ── Locate the NDK ────────────────────────────────────────────────────────────
if(NOT DEFINED ANDROID_NDK)
    foreach(_envvar ANDROID_NDK ANDROID_NDK_HOME)
        if(DEFINED ENV{${_envvar}} AND IS_DIRECTORY "$ENV{${_envvar}}")
            set(ANDROID_NDK "$ENV{${_envvar}}")
            break()
        endif()
    endforeach()
    if(NOT DEFINED ANDROID_NDK AND DEFINED ENV{ANDROID_HOME})
        # SDK-style layout: $ANDROID_HOME/ndk/<version>
        file(GLOB _NDK_CANDIDATES LIST_DIRECTORIES true
            "$ENV{ANDROID_HOME}/ndk/*"
            "$ENV{ANDROID_HOME}/ndk-bundle")
        list(SORT _NDK_CANDIDATES ORDER DESCENDING)
        foreach(_C IN LISTS _NDK_CANDIDATES)
            if(EXISTS "${_C}/build/cmake/android.toolchain.cmake")
                set(ANDROID_NDK "${_C}")
                break()
            endif()
        endforeach()
    endif()
endif()

if(NOT DEFINED ANDROID_NDK OR NOT EXISTS "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    message(FATAL_ERROR
        "\n"
        "  ======================================================================\n"
        "  Android NDK Not Found\n"
        "  ======================================================================\n"
        "\n"
        "  Set ANDROID_NDK to the NDK root directory, e.g.:\n"
        "    cmake -DANDROID_NDK=/path/to/ndk/<version> ...\n"
        "\n"
        "  Or set one of these environment variables:\n"
        "    export ANDROID_NDK=/path/to/ndk/<version>\n"
        "    export ANDROID_NDK_HOME=/path/to/ndk/<version>\n"
        "    export ANDROID_HOME=/path/to/sdk  (ndk/ subdirectory must exist)\n"
        "\n"
        "  Install via Android Studio:\n"
        "    SDK Manager -> SDK Tools -> NDK (Side by side)\n"
        "  ======================================================================\n"
    )
endif()

message(STATUS "Android NDK : ${ANDROID_NDK}")

# ── Defaults (override from command line as needed) ───────────────────────────

# Target ABI — arm64-v8a is the primary ABI for modern Android devices.
if(NOT DEFINED ANDROID_ABI)
    set(ANDROID_ABI "arm64-v8a")
endif()

# Minimum Android API level.
# 24 = Android 7.0 Nougat — lowest level with full Vulkan 1.0 support.
if(NOT DEFINED ANDROID_PLATFORM)
    set(ANDROID_PLATFORM "android-24")
endif()

# C++ STL — c++_shared provides a full C++17 standard library.
# Use c++_static if you need a self-contained .so with no runtime dependency.
if(NOT DEFINED ANDROID_STL)
    set(ANDROID_STL "c++_shared")
endif()

# NDK toolchain version (blank = NDK default clang)
# set(ANDROID_TOOLCHAIN clang)

message(STATUS "Android ABI      : ${ANDROID_ABI}")
message(STATUS "Android platform : ${ANDROID_PLATFORM}")
message(STATUS "Android STL      : ${ANDROID_STL}")

# ── Delegate to the NDK's official toolchain ──────────────────────────────────
include("${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
