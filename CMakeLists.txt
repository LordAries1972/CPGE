cmake_minimum_required(VERSION 3.20)
project(CrossPlatformGameEngine LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Game identity & renderer selection ───────────────────────────────────────
# GameName must match #define GAME_NAME in Includes.h and <GameName> in the vcxproj.
set(GAME_NAME "CPGE" CACHE STRING "Base game name used in the output executable")
# Renderer: DX11 | DX12 | OpenGL | Vulkan | METAL (Metal is macOS/iOS only; not implemented yet)
set(RENDERER "DX11" CACHE STRING "Active renderer (DX11, DX12, OpenGL, Vulkan, METAL)")

if(RENDERER STREQUAL "DX11")
    set(RENDERER_PREFIX "DX")
    set(RENDERER_DEFINE "__USE_DIRECTX_11__")
elseif(RENDERER STREQUAL "DX12")
    set(RENDERER_PREFIX "DX12")
    set(RENDERER_DEFINE "__USE_DIRECTX_12__")
elseif(RENDERER STREQUAL "OpenGL")
    set(RENDERER_PREFIX "OpenGL")
    set(RENDERER_DEFINE "__USE_OPENGL__")
elseif(RENDERER STREQUAL "Vulkan")
    set(RENDERER_PREFIX "Vulkan")
    set(RENDERER_DEFINE "__USE_VULKAN__")
elseif(RENDERER STREQUAL "METAL")
    set(RENDERER_PREFIX "METAL")
    set(RENDERER_DEFINE "__USE_METAL__")
else()
    message(FATAL_ERROR "Unknown RENDERER '${RENDERER}'. Valid values: DX11, DX12, OpenGL, Vulkan")
endif()

# Platform-appropriate output name:
#   Windows  : DX{name}.exe / OpenGL{name}.exe / Vulkan{name}.exe  (.exe added by CMake)
#   macOS    : OpenGL{name}.app / METAL{name}.app   (bundle; .app added by CMake)
#   iOS      : {name}.app bundle via Xcode / METAL signing pipeline
#   Linux    : OpenGL{name} / Vulkan{name}  (no extension on ELF)
#   Android  : library name only; packaging to .apk is handled by the Android build toolchain
set(EXE_OUTPUT_NAME "${RENDERER_PREFIX}${GAME_NAME}")

message(STATUS "Game name  : ${GAME_NAME}")
message(STATUS "Renderer   : ${RENDERER}  (define: ${RENDERER_DEFINE})")
message(STATUS "Output exe : ${EXE_OUTPUT_NAME}")

# ── x86/64 assembly (MASM) -- Windows/MSVC only ──────────────────────────────
# ml64.exe (MASM for x64) is part of the VC++ build tools; CMake discovers it
# automatically when ASM_MASM is enabled.  Must be called before add_executable.
if(MSVC AND WIN32)
    enable_language(ASM_MASM)
endif()

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    BlenderImports.cpp
    FBXImport.cpp
    Configuration.cpp
    ConsoleWindow.cpp
    Debug.cpp
    DX11Renderer.cpp
    DX12Renderer.cpp
    DX12RenderFrame.cpp
    DXCamera.cpp
    DXRenderFrame.cpp
    FXManager.cpp
    DX12Models.cpp
    ExceptionHandler.cpp
    FileIO.cpp
    GamePlayer.cpp
    GamingAI.cpp
    FBXAnimator.cpp
    GLTFAnimator.cpp
    ModelAnimator.cpp
    GUIConfigWindow.cpp
    GUIFileDialog.cpp
    GUIManager.cpp
    GUIWindows.cpp
    IOLoaderThread.cpp
    Joystick.cpp
    KBHandlersCode.cpp
    KeyboardHandler.cpp
    Lights.cpp
    main.cpp
    MathPrecalculation.cpp
    Models.cpp
    MoviePlayer.cpp
    MyRandomizer.cpp
    NetworkManager.cpp
    OpenGLCamera.cpp
    OpenGLModels.cpp
    OpenGLRenderer.cpp
    OpenGLRenderFrame.cpp
    VulkanCamera.cpp
    VulkanModels.cpp
    VULKAN_Renderer.cpp
    VULKAN_RenderFrame.cpp
    Physics.cpp
    PUNPack.cpp
    RendererFactory.cpp
    SceneManager.cpp
    ScriptManager.cpp
    ScreenRecorder.cpp
    ShaderLoaders.cpp
    ShaderManager.cpp
    SoundManager.cpp
    ThreadManager.cpp
    TTSManager.cpp
    WinMediaPlayer.cpp
    WinSystem.cpp
    XMMODPlayer.cpp
    CrossPlatformGameEngine.rc
)

# x86/64 assembly optimisation routines (assembly.asm) -- ml64.exe / MASM only
if(MSVC AND WIN32)
    list(APPEND SOURCES assembly.asm)
endif()

add_executable(CrossPlatformGameEngine WIN32 ${SOURCES})

# ── Output file name ──────────────────────────────────────────────────────────
set_target_properties(CrossPlatformGameEngine PROPERTIES
    OUTPUT_NAME "${EXE_OUTPUT_NAME}"
)

# Set VS debugger working directory to project root so Assets/Shaders are found at runtime.
# Without this, VS defaults to the vcxproj output dir which has no Assets/ subtree.
if(MSVC)
    set_target_properties(CrossPlatformGameEngine PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

# macOS / iOS: wrap as an .app bundle (Xcode/CPack turns this into .dmg/.ipa for distribution)
if(APPLE)
    set_target_properties(CrossPlatformGameEngine PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "${EXE_OUTPUT_NAME}"
    )
endif()

# ── Vulkan SDK detection ──────────────────────────────────────────────────────
# Relies on VULKAN_SDK environment variable set by the Vulkan SDK installer.
# Falls back to scanning common drive-root install locations if env var is absent.
if(WIN32)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        file(GLOB _VK_CANDIDATES LIST_DIRECTORIES true
            "C:/VulkanSDK/*" "D:/VulkanSDK/*" "E:/VulkanSDK/*"
            "F:/VulkanSDK/*" "G:/VulkanSDK/*"
            "$ENV{ProgramFiles}/VulkanSDK/*"
            "$ENV{ProgramW6432}/VulkanSDK/*")
        foreach(_C IN LISTS _VK_CANDIDATES)
            if(EXISTS "${_C}/Include/vulkan/vulkan.h")
                set(ENV{VULKAN_SDK} "${_C}")
                find_package(Vulkan QUIET)
                if(Vulkan_FOUND)
                    break()
                endif()
            endif()
        endforeach()
    endif()
    if(NOT Vulkan_FOUND)
        message(FATAL_ERROR
            "\n"
            "  ======================================================================\n"
            "  CANNOT Compile Resources - Vulkan SDK Not Found\n"
            "  ======================================================================\n"
            "\n"
            "  The Vulkan SDK (vulkan.h / vulkan-1.lib) could not be located.\n"
            "  The VULKAN_SDK environment variable is not set, and no SDK was found\n"
            "  in the expected drive-root locations (C:..G:\\VulkanSDK\\*).\n"
            "\n"
            "  -- OPTION 1: Install the LunarG Vulkan SDK (recommended) -------------\n"
            "    Download from:  https://vulkan.lunarg.com/sdk/home#windows\n"
            "    Run the installer -- it sets VULKAN_SDK automatically.\n"
            "    After install, restart your terminal and re-run cmake-build.bat\n"
            "\n"
            "  -- OPTION 2: Set VULKAN_SDK manually ---------------------------------\n"
            "    set VULKAN_SDK=C:\\VulkanSDK\\<version>   (in a cmd / bat)\n"
            "    or via System Properties -> Environment Variables\n"
            "    Then re-run cmake-build.bat\n"
            "\n"
            "  Expected SDK layout:\n"
            "    <VULKAN_SDK>\\Include\\vulkan\\vulkan.h\n"
            "    <VULKAN_SDK>\\Lib\\vulkan-1.lib\n"
            "  ======================================================================\n"
        )
    endif()
    get_filename_component(_VK_LIB_DIR "${Vulkan_LIBRARIES}" DIRECTORY)
    message(STATUS "Vulkan SDK include : ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "Vulkan SDK lib dir : ${_VK_LIB_DIR}")

    # Extract and display the Vulkan SDK version from the include path.
    # The SDK installs vulkan/vulkan_core.h which defines VK_HEADER_VERSION_COMPLETE.
    # We parse the version from the path component as a quick reliable approach.
    if(DEFINED ENV{VULKAN_SDK})
        string(REGEX REPLACE ".*[/\\\\]([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1"
               _VK_SDK_VERSION "$ENV{VULKAN_SDK}")
        message(STATUS "Vulkan SDK version : ${_VK_SDK_VERSION}")
    elseif(Vulkan_INCLUDE_DIRS)
        string(REGEX REPLACE ".*[/\\\\]([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1"
               _VK_SDK_VERSION "${Vulkan_INCLUDE_DIRS}")
        message(STATUS "Vulkan SDK version : ${_VK_SDK_VERSION}")
    endif()
endif()

# ── OpenGL version detection ──────────────────────────────────────────────────
# Strategy: read the bundled GLEW header (always present at include/GL/glew.h)
# and find the highest GLEW_VERSION_X_Y define — this tells us the maximum GL
# API version the headers expose.  Works identically on Windows and Linux.
# We also call find_package(OpenGL) to get the system library path, but we do
# NOT rely on OPENGL_INCLUDE_DIR for version info because CMake often leaves it
# empty on Windows (opengl32 is a system implicit library).
find_package(OpenGL QUIET)
if(OpenGL_FOUND OR OPENGL_FOUND)
    if(OPENGL_gl_LIBRARY)
        message(STATUS "OpenGL library     : ${OPENGL_gl_LIBRARY}")
    elseif(OPENGL_LIBRARIES)
        message(STATUS "OpenGL library     : ${OPENGL_LIBRARIES}")
    else()
        message(STATUS "OpenGL library     : (system implicit)")
    endif()
else()
    message(STATUS "OpenGL library     : (system implicit -- opengl32 on Windows, libGL on Linux)")
endif()

# Scan the bundled GLEW header for the highest GLEW_VERSION_X_Y definition.
# GLEW_VERSION_X_Y is defined for every GL core version the GLEW release covers.
# Finding the largest X.Y tells us the max GL API version available to the build.
set(_GLEW_HDR "${CMAKE_CURRENT_SOURCE_DIR}/include/GL/glew.h")
if(EXISTS "${_GLEW_HDR}")
    file(STRINGS "${_GLEW_HDR}" _GLEW_VER_LINES
         REGEX "^#define GLEW_VERSION_[0-9]+_[0-9]+ ")
    set(_GLEW_MAX_MAJOR 0)
    set(_GLEW_MAX_MINOR 0)
    foreach(_LINE IN LISTS _GLEW_VER_LINES)
        string(REGEX MATCH "GLEW_VERSION_([0-9]+)_([0-9]+)" _GLEW_M "${_LINE}")
        if(_GLEW_M)
            set(_MAJ ${CMAKE_MATCH_1})
            set(_MIN ${CMAKE_MATCH_2})
            if(_MAJ GREATER _GLEW_MAX_MAJOR OR
               (_MAJ EQUAL   _GLEW_MAX_MAJOR AND _MIN GREATER _GLEW_MAX_MINOR))
                set(_GLEW_MAX_MAJOR ${_MAJ})
                set(_GLEW_MAX_MINOR ${_MIN})
            endif()
        endif()
    endforeach()
    if(_GLEW_MAX_MAJOR GREATER 0)
        message(STATUS "OpenGL (GLEW hdr)  : max API ${_GLEW_MAX_MAJOR}.${_GLEW_MAX_MINOR}  [${_GLEW_HDR}]")
    else()
        message(STATUS "OpenGL (GLEW hdr)  : no GLEW_VERSION_X_Y defines found in ${_GLEW_HDR}")
    endif()
else()
    message(STATUS "OpenGL (GLEW hdr)  : include/GL/glew.h not found -- expected at ${_GLEW_HDR}")
endif()

# Linux / macOS extra: pkg-config can report the system GL and GLEW package versions.
if(NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(_PKG_GL   QUIET gl)
        pkg_check_modules(_PKG_GLEW QUIET glew)
        if(_PKG_GL_FOUND)
            message(STATUS "OpenGL (pkg-config): gl ${_PKG_GL_VERSION}")
        endif()
        if(_PKG_GLEW_FOUND)
            message(STATUS "GLEW   (pkg-config): glew ${_PKG_GLEW_VERSION}")
        endif()
    endif()
endif()

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(CrossPlatformGameEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    $<$<BOOL:${Vulkan_FOUND}>:${Vulkan_INCLUDE_DIRS}>
)

# ── Library directories ───────────────────────────────────────────────────────
target_link_directories(CrossPlatformGameEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/steam/lib/win64
    $<$<BOOL:${Vulkan_FOUND}>:${_VK_LIB_DIR}>
)

# ── shaderc (GLSL→SPIR-V runtime compiler, bundled in Vulkan SDK) ────────────
if(Vulkan_FOUND AND WIN32)
    # Use the DLL import lib — avoids static-CRT mismatch with the combined libs
    target_link_libraries(CrossPlatformGameEngine PRIVATE
        $<$<CONFIG:Debug>:shaderc_sharedd>
        $<$<NOT:$<CONFIG:Debug>>:shaderc_shared>
    )
    # Copy shaderc DLL to the output directory so the exe finds it at runtime
    get_filename_component(_VK_BIN_DIR "${Vulkan_LIBRARIES}" DIRECTORY)
    string(REPLACE "/Lib" "/Bin" _VK_BIN_DIR "${_VK_BIN_DIR}")
    add_custom_command(TARGET CrossPlatformGameEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${_VK_BIN_DIR}/shaderc_sharedd.dll,${_VK_BIN_DIR}/shaderc_shared.dll>"
            "$<TARGET_FILE_DIR:CrossPlatformGameEngine>"
        COMMENT "Copying shaderc DLL to output directory"
    )
endif()

# ── Entry point ───────────────────────────────────────────────────────────────
if(MSVC)
    set_target_properties(CrossPlatformGameEngine PROPERTIES
        LINK_FLAGS "/ENTRY:WinMainCRTStartup"
    )
endif()

# ── MSVC compile options ──────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(CrossPlatformGameEngine PRIVATE
        # CXX-only flags: ml64.exe (MASM) does not understand MSVC CL options,
        # so every C++-specific flag must be guarded with $<COMPILE_LANGUAGE:CXX>.
        $<$<COMPILE_LANGUAGE:CXX>:/W3>          # Warning level 3
        $<$<COMPILE_LANGUAGE:CXX>:/wd4244>      # Disable C4244 (conversion, possible data loss)
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
        $<$<COMPILE_LANGUAGE:CXX>:/MP>          # Multi-processor compilation
        $<$<COMPILE_LANGUAGE:CXX>:/nologo>
        $<$<COMPILE_LANGUAGE:CXX>:/Zp16>        # Struct member alignment 16 bytes
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/MDd>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/sdl>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/FS>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/MD>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/sdl>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Oi>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Ob2>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/GF>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/GT>
        # ASM_MASM flags: only /nologo is meaningful to ml64.exe here
        $<$<COMPILE_LANGUAGE:ASM_MASM>:/nologo>
    )

    target_compile_definitions(CrossPlatformGameEngine PRIVATE
        $<$<CONFIG:Debug>:_DEBUG;_CONSOLE>
        $<$<CONFIG:Release>:NDEBUG;_CONSOLE>
        UNICODE
        _UNICODE
        GLEW_STATIC     # Use glew32s.lib (static); prevents __declspec(dllimport) on GLEW symbols
        ${RENDERER_DEFINE}  # e.g. __USE_DIRECTX_12__ -- embedded in vcxproj so stale .obj from other pipelines cannot sneak in
    )

    # Release: whole-program optimisation
    set_target_properties(CrossPlatformGameEngine PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
    )

    # Release: COMDAT folding + dead-code stripping
    target_link_options(CrossPlatformGameEngine PRIVATE
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /NODEFAULTLIB:LIBCMT>   # glew32s.lib (/MT) pulls in LIBCMT; project uses /MD -- suppress LNK4098
        $<$<CONFIG:Debug>:/DEBUG /ASSEMBLYDEBUG /NODEFAULTLIB:LIBCMT>  # same issue in Debug (/MDd vs LIBCMT from glew32s.lib)
        /ignore:4099    # glew32s.lib has no PDB — suppress "PDB was not found" noise
    )

    # Debug: generate map file
    set_target_properties(CrossPlatformGameEngine PROPERTIES
        LINK_FLAGS_DEBUG "/MAP:${CMAKE_CURRENT_SOURCE_DIR}/CrossPlatformGameEngine.map"
    )
endif()

# ── Shader pre-compilation ───────────────────────────────────────────────────
# Compiles all HLSL shaders to .cso files as the first PRE_BUILD step.
# Works with VS 2022, VS Build Tools, or a standalone Windows SDK install.
# .cso files land next to the executable for each configuration.
if(WIN32)
    set(_FXC_HINTS "")

    # Priority 1 — VS / Build Tools developer environment variables.
    # Set by vcvars64.bat; always point to the exact SDK version in use.
    if(DEFINED ENV{WindowsSdkBinPath})
        string(REGEX REPLACE "[\\/]+$" "" _B "$ENV{WindowsSdkBinPath}")
        list(APPEND _FXC_HINTS "${_B}/x64")
    endif()
    if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
        string(REGEX REPLACE "[\\/]+$" "" _D "$ENV{WindowsSdkDir}")
        string(REGEX REPLACE "[\\/]+$" "" _V "$ENV{WindowsSDKVersion}")
        list(APPEND _FXC_HINTS "${_D}/bin/${_V}/x64")
    endif()

    # Priority 2 — CMake VS-generator detected version, both PF locations.
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        foreach(_PF "$ENV{ProgramFiles\(x86\)}" "C:/Program Files (x86)"
                    "$ENV{ProgramW6432}"         "C:/Program Files")
            list(APPEND _FXC_HINTS
                "${_PF}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
        endforeach()
    endif()

    # Priority 3 — Windows registry KitsRoot10 key.
    # Present on any machine with the standalone SDK installed, even without VS.
    get_filename_component(_SDK_REG
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]"
        ABSOLUTE)
    if(_SDK_REG AND IS_DIRECTORY "${_SDK_REG}")
        file(GLOB _SDK_REG_VERS LIST_DIRECTORIES true "${_SDK_REG}/bin/10.*")
        foreach(_D IN LISTS _SDK_REG_VERS)
            list(APPEND _FXC_HINTS "${_D}/x64")
        endforeach()
    endif()

    # Priority 4 — broad glob across both Program Files roots (catches any version).
    foreach(_PF_BIN
            "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin"
            "C:/Program Files (x86)/Windows Kits/10/bin"
            "$ENV{ProgramW6432}/Windows Kits/10/bin"
            "C:/Program Files/Windows Kits/10/bin")
        file(GLOB _G LIST_DIRECTORIES true "${_PF_BIN}/10.*")
        foreach(_D IN LISTS _G)
            list(APPEND _FXC_HINTS "${_D}/x64")
        endforeach()
    endforeach()

    find_program(FXC_EXE NAMES fxc HINTS ${_FXC_HINTS} DOC "HLSL shader compiler")

    if(NOT FXC_EXE)
        message(FATAL_ERROR
            "\n"
            "  ======================================================================\n"
            "  CANNOT Compile Resources - Please Install MSSDK\n"
            "  ======================================================================\n"
            "\n"
            "  fxc.exe (DirectX HLSL shader compiler) was not found on this system.\n"
            "  It is required to pre-compile HLSL shaders (.hlsl -> .cso) and is\n"
            "  included with the Microsoft Windows 10/11 SDK.\n"
            "\n"
            "  -- OPTION 1: Install via Visual Studio Installer (recommended) --------\n"
            "    1. Open the Visual Studio Installer\n"
            "       (search 'Visual Studio Installer' in the Start menu)\n"
            "    2. Click 'Modify' on your Visual Studio or Build Tools installation\n"
            "    3. Select the 'Individual Components' tab\n"
            "    4. Search for 'Windows SDK' and tick the latest entry, e.g.:\n"
            "         'Windows 11 SDK (10.0.26100.0)'\n"
            "    5. Click 'Modify' and wait for the installation to complete\n"
            "    6. Re-run cmake-build.bat\n"
            "\n"
            "  -- OPTION 2: Standalone Windows SDK Installer -------------------------\n"
            "    Download from:\n"
            "      https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/\n"
            "    During installation select at minimum:\n"
            "      - Windows SDK Signing Tools for Desktop Apps\n"
            "      - Windows SDK for Desktop C++ x86 Apps\n"
            "      - Windows SDK for Desktop C++ amd64 Apps\n"
            "    After install, re-run cmake-build.bat\n"
            "\n"
            "  -- NOTES --------------------------------------------------------------\n"
            "    fxc.exe will be installed to:\n"
            "      C:\\Program Files (x86)\\Windows Kits\\10\\bin\\<version>\\x64\\fxc.exe\n"
            "    This project targets Windows 10 / Windows 11 (x64).\n"
            "    A minimum SDK version of 10.0.19041.0 is required.\n"
            "  ======================================================================\n"
        )
    endif()

    message(STATUS "Shader compiler: ${FXC_EXE}")

    find_program(DXC_EXE
        NAMES dxc
        HINTS
            "$ENV{VULKAN_SDK}/bin"
            "$ENV{VULKAN_SDK}/Bin"
        DOC "DirectX Shader Compiler for SM6 DXIL shaders")

    if(DXC_EXE)
        message(STATUS "DX12 native shader compiler: ${DXC_EXE}")
    else()
        message(WARNING "dxc.exe was not found; DX12 native SM6 shaders will not be precompiled and runtime will fall back to FXC CSOs.")
    endif()

    set(_SD "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders")

    # Compile one HLSL to .cso alongside the executable
    macro(add_hlsl_shader HLSL_FILE PROFILE OUT_CSO)
        add_custom_command(TARGET CrossPlatformGameEngine PRE_BUILD
            COMMAND "${FXC_EXE}"
                /nologo
                /T ${PROFILE}
                /E main
                $<IF:$<CONFIG:Debug>,/Od,/O1>
                /Fo "$<TARGET_FILE_DIR:CrossPlatformGameEngine>/${OUT_CSO}"
                "${_SD}/${HLSL_FILE}"
            COMMENT "FXC: ${HLSL_FILE} -> ${OUT_CSO}"
            VERBATIM
        )
    endmacro()

    macro(add_dxc_shader HLSL_FILE PROFILE OUT_CSO)
        if(DXC_EXE)
            add_custom_command(TARGET CrossPlatformGameEngine PRE_BUILD
                COMMAND "${DXC_EXE}"
                    -nologo
                    -T ${PROFILE}
                    -E main
                    $<IF:$<CONFIG:Debug>,-Od,-O3>
                    -Fo "$<TARGET_FILE_DIR:CrossPlatformGameEngine>/${OUT_CSO}"
                    "${_SD}/${HLSL_FILE}"
                COMMENT "DXC: ${HLSL_FILE} -> ${OUT_CSO}"
                VERBATIM
            )
        endif()
    endmacro()

    # ── DirectX HLSL shaders compiled via FXC (Windows SDK) ──────────────────
    message(STATUS "[Shaders] DirectX: fxc.exe compiling HLSL shaders")
    add_hlsl_shader(DefaultVertex.hlsl  vs_5_0  VertexShader.cso)
    add_hlsl_shader(DefaultPixel.hlsl   ps_5_0  PixelShader.cso)
    add_hlsl_shader(ModelVertex.hlsl    vs_5_0  ModelVShader.cso)
    add_hlsl_shader(ModelPixel.hlsl     ps_5_0  ModelPShader.cso)

    # Native DirectX 12 shaders are DXIL/SM6 assets. They are selected only on
    # FL12_x + SM6 adapters; FL11_x DX12 API devices keep using the FXC CSOs.
    add_dxc_shader(DX12NativeModelVertex.hlsl  vs_6_0  DX12NativeModelVShader.cso)
    add_dxc_shader(DX12NativeModelPixel.hlsl   ps_6_0  DX12NativeModelPShader.cso)

    # ── OpenGL GLSL shaders — copy source to output dir (runtime-loaded) ─────
    # OpenGL loads GLSL text at runtime; no compilation step needed.
    # The Windows SDK opengl32.lib / glu32.lib provides the OpenGL implementation.
    message(STATUS "[Shaders] OpenGL: GLSL source files copied to output dir (loaded at runtime)")
    macro(copy_glsl_shader GLSL_FILE)
        add_custom_command(TARGET CrossPlatformGameEngine POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_SD}/${GLSL_FILE}"
                "$<TARGET_FILE_DIR:CrossPlatformGameEngine>/${GLSL_FILE}"
            COMMENT "GLSL copy: ${GLSL_FILE}"
            VERBATIM
        )
    endmacro()

    copy_glsl_shader(ModelVertex.glsl)
    copy_glsl_shader(ModelPixel.glsl)

    # ── Vulkan SPIR-V shaders compiled via glslangValidator or shaderc ────────
    # Preferred: glslangValidator from the Vulkan SDK (set by VULKAN_SDK env var).
    # If not found the source .vert/.frag files are copied raw and VulkanModels.cpp
    # will compile them at runtime via the shaderc API (if available).
    if(Vulkan_FOUND)
        find_program(GLSLANG_EXE
            NAMES glslangValidator
            HINTS
                "$ENV{VULKAN_SDK}/bin"
                "$ENV{VULKAN_SDK}/Bin"
            DOC "Vulkan GLSL → SPIR-V compiler"
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
                # glslangValidator absent — copy GLSL source; shaderc will compile at runtime.
                add_custom_command(TARGET CrossPlatformGameEngine POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_SD}/${GLSL_FILE}"
                        "$<TARGET_FILE_DIR:CrossPlatformGameEngine>/${GLSL_FILE}"
                    COMMENT "Vulkan GLSL copy (no glslangValidator): ${GLSL_FILE}"
                    VERBATIM
                )
                message(STATUS "[Shaders] Vulkan: glslangValidator not found -- ${GLSL_FILE} will be compiled at runtime via shaderc")
            endif()
        endmacro()

        compile_vulkan_shader(ModelVertex.vert  ModelVertex.vert.spv)
        compile_vulkan_shader(ModelPixel.frag   ModelPixel.frag.spv)
    endif()
endif()

# ── Version auto-increment (mirrors UpdateVersionFile MSBuild target) ─────────
set(VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Version.id")
add_custom_command(TARGET CrossPlatformGameEngine PRE_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_FILE=${VERSION_FILE}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IncrementVersion.cmake"
    COMMENT "Incrementing build version"
)
