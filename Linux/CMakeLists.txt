cmake_minimum_required(VERSION 3.20)
# C is enabled as well as C++: the Android build compiles the NDK's
# android_native_app_glue.c (a plain C file) into the shared library.
project(CrossPlatformGameEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Game identity & renderer selection ───────────────────────────────────────
# Linux / Android supports OpenGL and Vulkan only (no DirectX).
set(GAME_NAME "CPGE" CACHE STRING "Base game name used in the output executable")
set(RENDERER "OpenGL" CACHE STRING "Active renderer (OpenGL, Vulkan)")

# ── Platform detection ────────────────────────────────────────────────────────
# NOTE: PLATFORM_WINDOWS / PLATFORM_LINUX / PLATFORM_ANDROID are self-defined by
# Platform.h from the compiler macros (_WIN32 / __ANDROID__ / __linux__), so no
# -D platform define is passed here (the old PLATFORM_DEFINE was unused).
if(ANDROID)
    set(PLATFORM_LABEL "Android")
else()
    set(PLATFORM_LABEL "Linux")
endif()

# ── Renderer mapping ──────────────────────────────────────────────────────────
if(RENDERER STREQUAL "OpenGL")
    set(RENDERER_PREFIX "OpenGL")
    set(RENDERER_DEFINE "__USE_OPENGL__")
elseif(RENDERER STREQUAL "Vulkan")
    set(RENDERER_PREFIX "Vulkan")
    set(RENDERER_DEFINE "__USE_VULKAN__")
else()
    message(FATAL_ERROR
        "Unknown RENDERER '${RENDERER}'.\n"
        "Valid values for Linux/Android: OpenGL, Vulkan\n"
        "DirectX is Windows-only — use the root CMakeLists.txt + cmake-build.bat for Windows builds.")
endif()

# Source root is the parent directory (Linux/ is a sub-folder of the project root)
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(EXE_OUTPUT_NAME "${RENDERER_PREFIX}${GAME_NAME}")
# Android: one fixed library name for every renderer so the manifest's
# android.app.lib_name never depends on which renderer was built
# (each renderer build lands in its own build-android/<Renderer>/ dir).
if(ANDROID)
    set(EXE_OUTPUT_NAME "CPGE")
endif()

message(STATUS "Platform   : ${PLATFORM_LABEL}")
message(STATUS "Game name  : ${GAME_NAME}")
message(STATUS "Renderer   : ${RENDERER}  (define: ${RENDERER_DEFINE})")
message(STATUS "Output     : ${EXE_OUTPUT_NAME}")

# ── Sources ───────────────────────────────────────────────────────────────────
# Windows-only files are excluded:
#   DX11Renderer, DX12Renderer, DXCamera, DXRenderFrame,
#   WinMediaPlayer, WinSystem
# CrossPlatformGameEngine.rc (Windows resource file) is also excluded.
# Steam: no implementation in tree (see Docs/About-CPGE.md §38). The
#  Steamworks link library (libsteam_api.so) is not vendored; Steam stays
#  excluded until a commercial release is actively prepared.
# Renderer-specific sources are added per RENDERER below so the OpenGL build
# never compiles the Vulkan backend (and vice versa).
set(SOURCES
    ${SRC_DIR}/BlenderImports.cpp
    ${SRC_DIR}/Configuration.cpp
    ${SRC_DIR}/ConsoleWindow.cpp
    ${SRC_DIR}/Debug.cpp
    ${SRC_DIR}/ExceptionHandler.cpp
    ${SRC_DIR}/FBXImport.cpp
    ${SRC_DIR}/FileIO.cpp
    ${SRC_DIR}/GamePlayer.cpp
    ${SRC_DIR}/GamingAI.cpp
    ${SRC_DIR}/FBXAnimator.cpp
    ${SRC_DIR}/GLTFAnimator.cpp
    ${SRC_DIR}/ModelAnimator.cpp
    ${SRC_DIR}/GUIConfigWindow.cpp
    ${SRC_DIR}/GUIFileDialog.cpp
    ${SRC_DIR}/GUIManager.cpp
    ${SRC_DIR}/GUITemplates.cpp
    ${SRC_DIR}/GUIWindows.cpp
    ${SRC_DIR}/Joystick.cpp
    ${SRC_DIR}/KBHandlersCode.cpp
    ${SRC_DIR}/KeyboardHandler.cpp
    ${SRC_DIR}/Lights.cpp
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/MathPrecalculation.cpp
    ${SRC_DIR}/Models.cpp
    ${SRC_DIR}/MoviePlayer.cpp
    ${SRC_DIR}/MyRandomizer.cpp
    ${SRC_DIR}/NetworkManager.cpp
    ${SRC_DIR}/FXManager.cpp
    ${SRC_DIR}/IOLoaderThread.cpp
    ${SRC_DIR}/Physics.cpp
    ${SRC_DIR}/PUNPack.cpp
    ${SRC_DIR}/RendererFactory.cpp
    ${SRC_DIR}/SceneManager.cpp
    ${SRC_DIR}/ScriptManager.cpp
    ${SRC_DIR}/ScreenRecorder.cpp
    ${SRC_DIR}/ShaderLoaders.cpp
    ${SRC_DIR}/ShaderManager.cpp
    ${SRC_DIR}/SoundManager.cpp
    ${SRC_DIR}/MiniaudioStream.cpp   # miniaudio implementation (single TU) + the tracker players' streaming helper
    ${SRC_DIR}/ThreadManager.cpp
    ${SRC_DIR}/TTSManager.cpp
    ${SRC_DIR}/XMMODPlayer.cpp
    # Tracker players (MODPlayer is instantiated by main.cpp / IOLoaderThread.cpp;
    # the others are unreferenced under the current __USE_MODPLAYER__ config but
    # are part of the engine and must stay linkable if the config changes).
    ${SRC_DIR}/MODPlayer.cpp
    ${SRC_DIR}/ITPlayer.cpp
    ${SRC_DIR}/MPTMPlayer.cpp
    ${SRC_DIR}/S3MPlayer.cpp
    # Vector system (Phase2 headline feature; Vectors.cpp holds the non-inline
    # MyVector/MyQuaternion methods, the rest have no callers yet but are engine code).
    ${SRC_DIR}/Vectors.cpp
    ${SRC_DIR}/VectorShapes.cpp
    ${SRC_DIR}/VectorRadar.cpp
    ${SRC_DIR}/VectorRender.cpp
    ${SRC_DIR}/XMLParser.cpp
)

if(RENDERER STREQUAL "OpenGL")
    list(APPEND SOURCES
        ${SRC_DIR}/OpenGLCamera.cpp
        ${SRC_DIR}/OpenGLModels.cpp
        ${SRC_DIR}/OpenGLRenderFrame.cpp
        ${SRC_DIR}/OpenGLRenderer.cpp
    )
    # Per-platform OpenGL implementation (platform split, 24 Nov 2026): only the
    # file for the target platform is compiled; the file itself is the platform.
    if(ANDROID)
        list(APPEND SOURCES ${SRC_DIR}/android-opengl-render.cpp)
    else()
        list(APPEND SOURCES ${SRC_DIR}/linux-opengl-render.cpp)
    endif()
elseif(RENDERER STREQUAL "Vulkan")
    list(APPEND SOURCES
        ${SRC_DIR}/VulkanCamera.cpp
        ${SRC_DIR}/VulkanModels.cpp
        ${SRC_DIR}/VULKAN_RenderFrame.cpp
        ${SRC_DIR}/VULKAN_Renderer.cpp
    )
    # Per-platform Vulkan implementation (platform split, 24 Nov 2026): only the
    # file for the target platform is compiled; the file itself is the platform.
    if(ANDROID)
        list(APPEND SOURCES ${SRC_DIR}/android-vulkan-render.cpp)
    else()
        list(APPEND SOURCES ${SRC_DIR}/linux-vulkan-render.cpp)
    endif()
endif()

# Linux-only window-system implementation (SystemUtils for X11; WinSystem.cpp
# is Windows-only and stays out of this build).
if(NOT ANDROID)
    list(APPEND SOURCES ${SRC_DIR}/WinSystemLinux.cpp)
endif()

# Android-only: SystemUtils implementation (ANativeWindow / internal storage)
# and the shared android_app accessor used by main.cpp, FileIO and Debug.
# The MediaCodec backends (Phase D step 4) are Android-only too: the
# MoviePlayer decode backend and the ScreenRecorder encode backend.
if(ANDROID)
    list(APPEND SOURCES
        ${SRC_DIR}/AndroidApp.cpp
        ${SRC_DIR}/WinSystemAndroid.cpp
        ${SRC_DIR}/android-movieplayer.cpp
        ${SRC_DIR}/android-screenrecorder.cpp
    )
endif()

# ── Target ────────────────────────────────────────────────────────────────────
# Android produces a shared library loaded by NativeActivity / GameActivity.
# Linux produces a standard ELF executable.
if(ANDROID)
    # The NDK toolchain file locates the NDK itself, but it does not export
    # ANDROID_NDK as a CMake variable in every version, so resolve it here
    # (env vars first, then the Phase D install location) for the glue source
    # and include directory below.
    if(NOT ANDROID_NDK)
        if(DEFINED ENV{ANDROID_NDK} AND NOT "$ENV{ANDROID_NDK}" STREQUAL "")
            set(ANDROID_NDK "$ENV{ANDROID_NDK}")
        elseif(DEFINED ENV{ANDROID_NDK_HOME} AND NOT "$ENV{ANDROID_NDK_HOME}" STREQUAL "")
            set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
        elseif(DEFINED ENV{ANDROID_HOME} AND NOT "$ENV{ANDROID_HOME}" STREQUAL "")
            file(GLOB _ndk_candidates "$ENV{ANDROID_HOME}/ndk/*")
            list(SORT _ndk_candidates)
            list(GET _ndk_candidates -1 ANDROID_NDK)
        endif()
    endif()
    if(NOT ANDROID_NDK OR NOT EXISTS "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
        message(FATAL_ERROR
            "Android NDK native_app_glue not found (ANDROID_NDK=${ANDROID_NDK}).\n"
            "  The NDK is required for the NativeActivity app entry (android_main).")
    endif()
    message(STATUS "NDK (glue) : ${ANDROID_NDK}")

    # NativeActivity entry: the NDK ships android_native_app_glue as source;
    # compile it into the library so ANativeActivity_onCreate is exported and
    # the app's android_main() (main.cpp) is called on its own thread.
    list(APPEND SOURCES "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")

    add_library(CrossPlatformGameEngine SHARED ${SOURCES})
else()
    add_executable(CrossPlatformGameEngine ${SOURCES})
endif()

set_target_properties(CrossPlatformGameEngine PROPERTIES
    OUTPUT_NAME "${EXE_OUTPUT_NAME}"
)

# ── Compile definitions ───────────────────────────────────────────────────────
# The renderer define is injected here because Includes.h has the Linux/Android
# renderer defines commented out; passing via -D avoids needing to patch Includes.h.
target_compile_definitions(CrossPlatformGameEngine PRIVATE
    ${RENDERER_DEFINE}
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# ── GCC / Clang compile options ───────────────────────────────────────────────
target_compile_options(CrossPlatformGameEngine PRIVATE
    -Wall
    -Wno-unknown-pragmas  # 52 files carry MSVC #pragma warning / comment(lib, ...) — noise, not errors
    -Wno-narrowing
    -Wno-unused-variable
    -Wno-unused-function
    -fno-strict-aliasing
    -fvisibility=hidden
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O2 -ffunction-sections -fdata-sections>
)

# Dead-code strip on Linux Release builds
if(NOT ANDROID)
    target_link_options(CrossPlatformGameEngine PRIVATE
        $<$<CONFIG:Release>:-Wl,--gc-sections>
    )
endif()

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(CrossPlatformGameEngine PRIVATE
    ${SRC_DIR}
    ${SRC_DIR}/include
    ${SRC_DIR}/nlohmann
)

# ── Library dependencies ──────────────────────────────────────────────────────

# Threading (pthreads on Linux/Android)
find_package(Threads REQUIRED)
target_link_libraries(CrossPlatformGameEngine PRIVATE Threads::Threads)

# ── OpenGL / GLEW (Linux desktop) ────────────────────────────────────────────
if(NOT ANDROID)
    find_package(OpenGL REQUIRED)
    target_include_directories(CrossPlatformGameEngine PRIVATE ${OPENGL_INCLUDE_DIR})
    target_link_libraries(CrossPlatformGameEngine PRIVATE ${OPENGL_LIBRARIES})
    # GLX (used directly by OpenGLRenderer / OpenGLRenderFrame for context
    # creation and buffer swaps). Modern CMake provides OpenGL::GLX.
    if(TARGET OpenGL::GLX)
        target_link_libraries(CrossPlatformGameEngine PRIVATE OpenGL::GLX)
    endif()

    # X11 (window creation in main.cpp, input in KeyboardHandler, metrics in
    # WinSystemLinux.cpp). Required on Linux desktop — no X11, no window.
    find_package(X11 REQUIRED)
    target_include_directories(CrossPlatformGameEngine PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(CrossPlatformGameEngine PRIVATE ${X11_LIBRARIES})

    # GLEW — prefer find_package; fall back to manual link if not in CMake registry
    find_package(GLEW REQUIRED)
    target_compile_definitions(CrossPlatformGameEngine PRIVATE GLEW_STATIC)
    target_include_directories(CrossPlatformGameEngine PRIVATE ${GLEW_INCLUDE_DIRS})
    target_link_libraries(CrossPlatformGameEngine PRIVATE GLEW::GLEW)
    message(STATUS "GLEW      : ${GLEW_LIBRARIES}")

    # GLM (header-only math; required — VULKAN_Renderer.h, FXManager.h and the
    # cameras include <glm/glm.hpp> unconditionally, so a missing glm is a
    # hard compile error, not a warning). GLM is vendored in include/glm
    # (version 0.9.9.8, managed by Daniel — see ReleaseInfo.md, 24 Nov 2026);
    # ${SRC_DIR}/include is already on the include path for every build, so
    # no find_package or link step is needed. The check below fails fast with
    # a clear message if the vendored copy is ever removed.
    if(NOT EXISTS "${SRC_DIR}/include/glm/glm.hpp")
        message(FATAL_ERROR
            "Vendored GLM headers not found (expected include/glm/glm.hpp).\n"
            "  GLM is vendored in the repo — restore include/glm from version\n"
            "  control, or point the build at a GLM copy with GLM_ROOT.")
    endif()
endif()

# ── OpenGL ES / EGL (Android) ─────────────────────────────────────────────────
# mediandk is the NDK media library (AMediaExtractor / AMediaCodec /
# AMediaMuxer) used by the Android MediaCodec backends (Phase D step 4).
if(ANDROID)
    target_link_libraries(CrossPlatformGameEngine PRIVATE
        EGL GLESv3 android log mediandk
    )

    # NativeActivity glue headers (android_native_app_glue.h for main.cpp).
    target_include_directories(CrossPlatformGameEngine PRIVATE
        "${ANDROID_NDK}/sources/android/native_app_glue")

    # ── GLM for Android ──────────────────────────────────────────────────────
    # GLM is header-only and the engine includes <glm/glm.hpp> unconditionally.
    # It is vendored in include/glm (version 0.9.9.8, managed by Daniel — see
    # ReleaseInfo.md, 24 Nov 2026), and that copy is the primary source, so
    # the Android build needs no system install and no setup on any host.
    # The host /usr/include must NEVER be added to the NDK compile: host
    # glibc headers would shadow the Bionic sysroot (proven by the step-1
    # probe — <features.h> resolved to the host copy and bits/wordsize.h went
    # missing). Stage a copy of the glm headers into the build directory
    # instead and include that. The copy is per build tree, so -clean wipes it
    # and the next configure re-stages it. find_package(glm) is not used here:
    # under the NDK toolchain CMAKE_FIND_ROOT_PATH_MODE_PACKAGE is ONLY, so
    # the host /usr/share/cmake/glm config is invisible, and its glm::glm
    # target would carry /usr/include into the compile anyway.
    # GLM_ROOT (env var or -DGLM_ROOT=...) is an override only — a different
    # GLM copy to use instead of the vendored one. It must be the folder
    # containing glm/ (the download root, not the glm/ subfolder itself).
    # The system paths are a last resort for hosts whose vendored copy was
    # removed.
    find_path(GLM_INCLUDE_DIR glm/glm.hpp
        HINTS ${SRC_DIR}/include $ENV{GLM_ROOT} ${GLM_ROOT} /usr/include /usr/local/include
        NO_CMAKE_FIND_ROOT_PATH
        NO_DEFAULT_PATH)
    if(NOT GLM_INCLUDE_DIR)
        message(FATAL_ERROR
            "GLM headers not found for the Android build.\n"
            "  GLM is vendored in include/glm — restore it from version control.\n"
            "  Or set GLM_ROOT to a GLM copy (the folder containing glm/),\n"
            "  or install it on the system (Linux: sudo apt install libglm-dev).")
    endif()
    file(COPY "${GLM_INCLUDE_DIR}/glm" DESTINATION "${CMAKE_BINARY_DIR}/android-deps")
    target_include_directories(CrossPlatformGameEngine PRIVATE "${CMAKE_BINARY_DIR}/android-deps")
    message(STATUS "GLM (Android): staged from ${GLM_INCLUDE_DIR}")
endif()

# ── Vulkan ────────────────────────────────────────────────────────────────────
find_package(Vulkan QUIET)
if(RENDERER STREQUAL "Vulkan")
    if(NOT Vulkan_FOUND)
        message(FATAL_ERROR
            "\n"
            "  ======================================================================\n"
            "  Vulkan SDK Not Found\n"
            "  ======================================================================\n"
            "\n"
            "  The Vulkan SDK (vulkan/vulkan.h / libvulkan) could not be located.\n"
            "  VULKAN_SDK environment variable is not set, and the system search failed.\n"
            "\n"
            "  -- OPTION 1: Install via package manager (Ubuntu/Debian) --------------\n"
            "    sudo apt install libvulkan-dev vulkan-tools\n"
            "\n"
            "  -- OPTION 2: Install LunarG Vulkan SDK ------------------------------\n"
            "    Download from: https://vulkan.lunarg.com/sdk/home#linux\n"
            "    Follow the installation instructions and set VULKAN_SDK.\n"
            "\n"
            "  -- OPTION 3: Set VULKAN_SDK manually --------------------------------\n"
            "    export VULKAN_SDK=/path/to/vulkansdk/x86_64\n"
            "    Then re-run cmake-build.sh\n"
            "  ======================================================================\n"
        )
    endif()
    target_include_directories(CrossPlatformGameEngine PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(CrossPlatformGameEngine PRIVATE ${Vulkan_LIBRARIES})
    message(STATUS "Vulkan SDK : ${Vulkan_INCLUDE_DIRS}")

    # shaderc — runtime GLSL → SPIR-V compilation.
    # Prefer the shared library: libshaderc_combined.a is a static archive
    # whose glslang / SPIRV-Tools symbols are NOT self-contained, so linking
    # it needs those libraries added separately (76 undefined references on
    # Ubuntu's libshaderc-dev). The shared lib (shaderc_shared in the LunarG
    # SDK, plain shaderc on Ubuntu) carries its dependencies inside, so it
    # links on its own. The combined archive stays as a last resort.
    find_library(SHADERC_LIB
        NAMES shaderc_shared shaderc shaderc_combined
        HINTS
            "$ENV{VULKAN_SDK}/lib"
            "$ENV{VULKAN_SDK}/Lib"
            /usr/lib /usr/local/lib
    )
    if(SHADERC_LIB)
        target_link_libraries(CrossPlatformGameEngine PRIVATE ${SHADERC_LIB})
        message(STATUS "shaderc    : ${SHADERC_LIB}")
    else()
        message(WARNING "shaderc not found — runtime GLSL→SPIR-V compilation disabled; pre-compiled .spv files required")
    endif()
endif()

# GLM is already required above for every Linux build (the cameras and
# FXManager.h include <glm/glm.hpp>); nothing more to do per-renderer here.

# ── XCB (Vulkan Linux surface only) ───────────────────────────────────────────
# VULKAN_Renderer.cpp creates its surface with vkCreateXcbSurfaceKHR, so the
# Vulkan build needs libxcb. The OpenGL build does not use xcb.
if(NOT ANDROID AND RENDERER STREQUAL "Vulkan")
    find_package(X11 REQUIRED)  # already required above; keeps xcb lookup ordered
    find_library(XCB_LIB xcb REQUIRED)
    target_link_libraries(CrossPlatformGameEngine PRIVATE ${XCB_LIB})
    message(STATUS "xcb        : ${XCB_LIB}")

    # X11-xcb (XGetXCBConnection): main.cpp shares the Xlib display connection
    # with the Vulkan renderer so vkCreateXcbSurfaceKHR gets a live
    # xcb_connection_t* (see main.cpp, Linux main()).
    find_library(X11_XCB_LIB X11-xcb REQUIRED)
    target_link_libraries(CrossPlatformGameEngine PRIVATE ${X11_XCB_LIB})
    message(STATUS "X11-xcb    : ${X11_XCB_LIB}")
endif()

# ── System libraries ──────────────────────────────────────────────────────────
if(NOT ANDROID)
    target_link_libraries(CrossPlatformGameEngine PRIVATE dl m)
    # stdc++fs needed for std::filesystem on GCC < 9; GCC 9+ links it automatically
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
        target_link_libraries(CrossPlatformGameEngine PRIVATE stdc++fs)
    endif()
endif()

# ── ALSA for SoundManager (Linux audio) ──────────────────────────────────────
if(NOT ANDROID)
    find_library(ALSA_LIB asound)
    if(ALSA_LIB)
        target_link_libraries(CrossPlatformGameEngine PRIVATE ${ALSA_LIB})
        message(STATUS "ALSA       : ${ALSA_LIB}")
    else()
        message(STATUS "ALSA       : not found (SoundManager may produce no audio on Linux)")
    endif()
endif()

# ── FFmpeg for MoviePlayer / ScreenRecorder / MPTM external samples ──────────
# FFmpeg is optional: the code detects the headers itself with __has_include
# (HAS_FFMPEG, like HAS_SHADERC) and reports "FFmpeg not available" when they
# are absent. CMake only links the libraries when BOTH the headers and the
# libraries are found, so a machine without FFmpeg dev files still builds.
#   Ubuntu/Debian: sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
#   (the -dev packages must match the installed libav* runtime version)
if(NOT ANDROID)
    find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
        HINTS /usr/include /usr/local/include)
    find_library(AVFORMAT_LIB avformat)
    find_library(AVCODEC_LIB avcodec)
    find_library(AVUTIL_LIB avutil)
    find_library(SWSCALE_LIB swscale)
    find_library(SWRESAMPLE_LIB swresample)
    if(FFMPEG_INCLUDE_DIR AND AVFORMAT_LIB AND AVCODEC_LIB AND AVUTIL_LIB AND SWSCALE_LIB AND SWRESAMPLE_LIB)
        target_include_directories(CrossPlatformGameEngine PRIVATE ${FFMPEG_INCLUDE_DIR})
        target_link_libraries(CrossPlatformGameEngine PRIVATE ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB} ${SWRESAMPLE_LIB})
        message(STATUS "FFmpeg     : ${AVFORMAT_LIB} (headers ${FFMPEG_INCLUDE_DIR})")
    else()
        message(STATUS "FFmpeg     : not found (MoviePlayer/ScreenRecorder disabled; MPTM external samples silent)")
    endif()
endif()

# ── eSpeak-ng for TTSManager (Linux text-to-speech) ──────────────────────────
# Same optional pattern as FFmpeg: the code detects the headers itself with
# __has_include (HAS_ESPEAK) and TTSManager reports STATE_ERROR when they
# are absent, so the engine runs without TTS instead of failing the build.
# CMake only links the library when BOTH the headers and the library are
# found. (Android uses the platform TextToSpeech via JNI in Phase D, so
# eSpeak-ng is not linked there.)
#   Ubuntu/Debian: sudo apt install libespeak-ng-dev espeak-ng
if(NOT ANDROID)
    find_path(ESPEAK_NG_INCLUDE_DIR espeak-ng/speak_lib.h
        HINTS /usr/include /usr/local/include)
    find_library(ESPEAK_NG_LIB espeak-ng)
    if(ESPEAK_NG_INCLUDE_DIR AND ESPEAK_NG_LIB)
        target_include_directories(CrossPlatformGameEngine PRIVATE ${ESPEAK_NG_INCLUDE_DIR})
        target_link_libraries(CrossPlatformGameEngine PRIVATE ${ESPEAK_NG_LIB})
        message(STATUS "eSpeak-ng  : ${ESPEAK_NG_LIB} (headers ${ESPEAK_NG_INCLUDE_DIR})")
    else()
        message(STATUS "eSpeak-ng  : not found (TTSManager disabled on Linux)")
    endif()
endif()

# ── Shader handling ───────────────────────────────────────────────────────────
set(_SD "${SRC_DIR}/Assets/Shaders")

# Vulkan: compile GLSL → SPIR-V with glslangValidator (or copy for runtime shaderc compile)
if(Vulkan_FOUND AND RENDERER STREQUAL "Vulkan")
    find_program(GLSLANG_EXE
        NAMES glslangValidator glslang
        HINTS
            "$ENV{VULKAN_SDK}/bin"
            /usr/bin /usr/local/bin
        DOC "Vulkan GLSL -> SPIR-V compiler"
    )

    macro(compile_vulkan_shader GLSL_FILE SPV_FILE)
        if(GLSLANG_EXE)
            add_custom_command(TARGET CrossPlatformGameEngine PRE_BUILD
                COMMAND "${GLSLANG_EXE}"
                    -V
                    "${_SD}/${GLSL_FILE}"
                    -o "$<TARGET_FILE_DIR:CrossPlatformGameEngine>/${SPV_FILE}"
                COMMENT "glslangValidator: ${GLSL_FILE} -> ${SPV_FILE}"
                VERBATIM
            )
            message(STATUS "[Shaders] Vulkan: glslangValidator compiling ${GLSL_FILE}")
        else()
            add_custom_command(TARGET CrossPlatformGameEngine POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_SD}/${GLSL_FILE}"
                    "$<TARGET_FILE_DIR:CrossPlatformGameEngine>/${GLSL_FILE}"
                COMMENT "Vulkan GLSL copy (no glslangValidator — runtime shaderc): ${GLSL_FILE}"
                VERBATIM
            )
            message(STATUS "[Shaders] Vulkan: glslangValidator not found — ${GLSL_FILE} copied; shaderc will compile at runtime")
        endif()
    endmacro()

    compile_vulkan_shader(ModelVertex.vert ModelVertex.vert.spv)
    compile_vulkan_shader(ModelPixel.frag  ModelPixel.frag.spv)
endif()

# OpenGL: copy GLSL source to output (loaded at runtime)
macro(copy_glsl_shader GLSL_FILE)
    add_custom_command(TARGET CrossPlatformGameEngine POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_SD}/${GLSL_FILE}"
            "$<TARGET_FILE_DIR:CrossPlatformGameEngine>/${GLSL_FILE}"
        COMMENT "GLSL copy: ${GLSL_FILE}"
        VERBATIM
    )
endmacro()

message(STATUS "[Shaders] OpenGL: GLSL source files copied to output dir (loaded at runtime)")
copy_glsl_shader(ModelVertex.glsl)
copy_glsl_shader(ModelPixel.glsl)

# ── Version auto-increment (shared cmake script with Windows build) ───────────
set(VERSION_FILE "${SRC_DIR}/Version.id")
add_custom_command(TARGET CrossPlatformGameEngine PRE_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_FILE=${VERSION_FILE}
        -P "${SRC_DIR}/cmake/IncrementVersion.cmake"
    COMMENT "Incrementing build version"
)

