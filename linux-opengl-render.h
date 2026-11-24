// -------------------------------------------------------------------------------------------------------------
// linux-opengl-render.h — Linux OpenGL implementation
// -------------------------------------------------------------------------------------------------------------
// Platform-specific implementation of the shared renderer class, split out of the
// shared renderer files so that working on one platform means working in one file.
//
// What lives here:
//   * Whole platform-dispatched member functions of the renderer class (context
//     creation, surface/window handling, fullscreen modes, text rendering backends,
//     present/swap and other per-platform behaviour).
//   * Small platform helper member functions factored out of shared functions.
//
// How the build selects this file:
//   * This file is the platform — it contains NO platform guards. It is compiled
//     only on its platform: Linux/CMakeLists.txt lists the linux-*.cpp / android-*.cpp
//     files in the matching renderer block, and CrossPlatformGameEngine.vcxproj lists
//     the windows-*.cpp files.
//   * Member declarations live in the shared class header (already platform-guarded
//     there where the member state is per-platform); this header documents the
//     members defined in the matching .cpp below.
//
// Rule (Daniel, 24 Nov 2026): Linux/Android code is separated from Windows code by
// condition directives, and Windows code is wrapped in PLATFORM_WINDOWS wherever it
// shares a function. These per-platform files ARE that separation — no platform
// guard is needed inside them.
// -------------------------------------------------------------------------------------------------------------
#pragma once

#if defined(__USE_OPENGL__)
#include "OpenGLRenderer.h"
#include "Debug.h"
#include "WinSystem.h"
#include "Configuration.h"
#include "FXManager.h"
#include "GUIManager.h"
#include "Models.h"
#include "OpenGLModels.h"
#include "Lights.h"
#include "SceneManager.h"
#include "MoviePlayer.h"
#include "KeyboardHandler.h"
// GLX_ARB_create_context tokens live in <GL/glxext.h>, which <GL/glx.h> does not pull
// in on all distros; define them here if they are missing (as OpenGLRenderer.cpp did).
#ifndef GLX_CONTEXT_MAJOR_VERSION_ARB
    #define GLX_CONTEXT_MAJOR_VERSION_ARB   0x2091
    #define GLX_CONTEXT_MINOR_VERSION_ARB   0x2092
    #define GLX_CONTEXT_FLAGS_ARB           0x2094
    #define GLX_CONTEXT_PROFILE_MASK_ARB    0x9126
    #define GLX_CONTEXT_DEBUG_BIT_ARB       0x00000001
    #define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif

// -------------------------------------------------------------------------------------------------------------
// Members defined in the matching .cpp (declared in the shared class header):
// -------------------------------------------------------------------------------------------------------------
//   CreateOpenGLContext(NativeWindowHandle) — GLX context creation (shared loader context)
//   InitializeOpenGLExtensions() — glewInit()
//   SetupPlatformSpecificContext() / CleanupPlatformSpecificContext() — GLX swap interval / teardown
//   SetFullScreen() / SetFullExclusive(int, int) / SetWindowedScreen() — return false (not applicable)
//   RenderTextToTexture(...) / RenderTextCenteredToTexture(...) — return 0 / delegate
//   DrawMyTextStyled(...) — delegates to DrawMyTextWithFont (Sans)
//   GetCharacterWidth(...) x2 / CalculateTextWidth(...) / CalculateTextHeight(...) — fallback estimates
//   InitPlatformTextBackend() / DetectPlatformGPUCaps() / ShutdownPlatformTextBackend() — no-op
//   RestorePlatformDisplayMode() — no-op
//   ReleasePlatformContext() — glXMakeCurrent(display, None, nullptr)
//   LoadTextureFromFilePlatform(...) — stb_image via OpenGLModelUtils
//   HasPlatformGLContext() / MakePlatformContextCurrent() / RebindPlatformContext() — GLX context
//   PresentPlatformFrame() — glXSwapBuffers
//   DrawIntroMovieFrame() — no-op (movie display is Vulkan-only on Linux)
//   SetPlatformLineSmoothing(bool) / SetPlatformMultisampling(bool) — GL_LINE_SMOOTH / GL_MULTISAMPLE on/off
//   SetupPlatformShadowTextureParams(GLenum, const float[4]) — CLAMP_TO_BORDER + border colour
//   SetPlatformDrawBufferNone() — glDrawBuffer(GL_NONE)

#endif // __USE_OPENGL__
