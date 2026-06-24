cmake_minimum_required(VERSION 3.20)
project(CrossPlatformGameEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Game identity & renderer selection ───────────────────────────────────────
# Linux / Android supports OpenGL and Vulkan only (no DirectX).
set(GAME_NAME "CPGE" CACHE STRING "Base game name used in the output executable")
set(RENDERER "OpenGL" CACHE STRING "Active renderer (OpenGL, Vulkan)")

# ── Platform detection ────────────────────────────────────────────────────────
if(ANDROID)
    set(PLATFORM_DEFINE "PLATFORM_ANDROID")
    set(PLATFORM_LABEL "Android")
else()
    set(PLATFORM_DEFINE "PLATFORM_LINUX")
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

message(STATUS "Platform   : ${PLATFORM_LABEL}")
message(STATUS "Game name  : ${GAME_NAME}")
message(STATUS "Renderer   : ${RENDERER}  (define: ${RENDERER_DEFINE})")
message(STATUS "Output     : ${EXE_OUTPUT_NAME}")

# ── Sources ───────────────────────────────────────────────────────────────────
# Windows-only files are excluded:
#   DX11Renderer, DX12Renderer, DXCamera, DXRenderFrame,
#   WinMediaPlayer, WinSystem
# CrossPlatformGameEngine.rc (Windows resource file) is also excluded.
# MySteam is excluded (deferred to release).
set(SOURCES
    ${SRC_DIR}/BlenderImports.cpp
    ${SRC_DIR}/Configuration.cpp
    ${SRC_DIR}/ConsoleWindow.cpp
    ${SRC_DIR}/Debug.cpp
    ${SRC_DIR}/ExceptionHandler.cpp
    ${SRC_DIR}/FileIO.cpp
    ${SRC_DIR}/GamePlayer.cpp
    ${SRC_DIR}/GamingAI.cpp
    ${SRC_DIR}/FBXAnimator.cpp
    ${SRC_DIR}/GLTFAnimator.cpp
    ${SRC_DIR}/ModelAnimator.cpp
    ${SRC_DIR}/GUIConfigWindow.cpp
    ${SRC_DIR}/GUIManager.cpp
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
    ${SRC_DIR}/OpenGLCamera.cpp
    ${SRC_DIR}/FXManager.cpp
    ${SRC_DIR}/OpenGLModels.cpp
    ${SRC_DIR}/OpenGLRenderFrame.cpp
    ${SRC_DIR}/OpenGLRenderer.cpp
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
    ${SRC_DIR}/ThreadManager.cpp
    ${SRC_DIR}/TTSManager.cpp
    ${SRC_DIR}/VulkanCamera.cpp
    ${SRC_DIR}/VulkanModels.cpp
    # IOLoaderThread.cpp already listed above — covers both OpenGL and Vulkan paths
    ${SRC_DIR}/VULKAN_RenderFrame.cpp
    ${SRC_DIR}/VULKAN_Renderer.cpp
    ${SRC_DIR}/XMMODPlayer.cpp
)

# ── Target ────────────────────────────────────────────────────────────────────
# Android produces a shared library loaded by NativeActivity / GameActivity.
# Linux produces a standard ELF executable.
if(ANDROID)
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

    # GLEW — prefer find_package; fall back to manual link if not in CMake registry
    find_package(GLEW QUIET)
    if(GLEW_FOUND)
        target_compile_definitions(CrossPlatformGameEngine PRIVATE GLEW_STATIC)
        target_include_directories(CrossPlatformGameEngine PRIVATE ${GLEW_INCLUDE_DIRS})
        target_link_libraries(CrossPlatformGameEngine PRIVATE GLEW::GLEW)
        message(STATUS "GLEW      : ${GLEW_LIBRARIES}")
    else()
        message(STATUS "GLEW      : find_package failed — linking -lGLEW directly")
        target_link_libraries(CrossPlatformGameEngine PRIVATE GLEW)
    endif()

    # GLM (header-only math; installed via package manager or bundled in include/)
    find_package(glm QUIET)
    if(glm_FOUND)
        target_link_libraries(CrossPlatformGameEngine PRIVATE glm::glm)
    else()
        find_path(GLM_INCLUDE_DIR glm/glm.hpp
            HINTS ${SRC_DIR}/include /usr/include /usr/local/include)
        if(GLM_INCLUDE_DIR)
            target_include_directories(CrossPlatformGameEngine PRIVATE ${GLM_INCLUDE_DIR})
        else()
            message(WARNING "GLM not found — install libglm-dev or add glm/ to ${SRC_DIR}/include/")
        endif()
    endif()
endif()

# ── OpenGL ES / EGL (Android) ─────────────────────────────────────────────────
if(ANDROID)
    target_link_libraries(CrossPlatformGameEngine PRIVATE
        EGL GLESv3 android log
    )
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

    # shaderc — runtime GLSL → SPIR-V compilation
    find_library(SHADERC_LIB
        NAMES shaderc_shared shaderc_combined shaderc
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

# GLM for Vulkan camera (VulkanCamera.cpp uses glm::*)
if(RENDERER STREQUAL "Vulkan" AND NOT glm_FOUND)
    find_package(glm QUIET)
    if(glm_FOUND)
        target_link_libraries(CrossPlatformGameEngine PRIVATE glm::glm)
    else()
        find_path(GLM_INCLUDE_DIR glm/glm.hpp
            HINTS ${SRC_DIR}/include /usr/include /usr/local/include)
        if(GLM_INCLUDE_DIR)
            target_include_directories(CrossPlatformGameEngine PRIVATE ${GLM_INCLUDE_DIR})
        else()
            message(WARNING "GLM not found — VulkanCamera.cpp requires glm/glm.hpp")
        endif()
    endif()
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
