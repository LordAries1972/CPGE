// -------------------------------------------------------------------------------------------------------------
// windows-opengl-render.h — Windows OpenGL implementation
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
// Windows-only: GDI+ (text rasterisation / texture decode) and DXGI (GPU caps).
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#pragma comment(lib, "glew32s.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dxgi.lib")
#include <gdiplus.h>
#include <dxgi.h>

// -------------------------------------------------------------------------------------------------------------
// Members defined in the matching .cpp (declared in the shared class header):
// -------------------------------------------------------------------------------------------------------------
//   FindMultisamplePixelFormat(HDC, const PIXELFORMATDESCRIPTOR&) — WGL multisample pixel format
//   CreateOpenGLContext(NativeWindowHandle) — WGL context creation
//   InitializeOpenGLExtensions() — GLEW initialisation
//   SetupPlatformSpecificContext() / CleanupPlatformSpecificContext() — WGL swap interval / teardown
//   EnforceFullscreenWindowPlacement(...) — Windows fullscreen window placement
//   SetFullScreen() / SetFullExclusive(int, int) / SetWindowedScreen() — Windows display modes
//   RenderTextToTexture(...) / RenderTextCenteredToTexture(...) / DrawMyTextStyled(...) — GDI text
//   GetCharacterWidth(...) x2 / CalculateTextWidth(...) / CalculateTextHeight(...) — GDI metrics
//   InitPlatformTextBackend() / ShutdownPlatformTextBackend() — GDI+ startup / shutdown
//   DetectPlatformGPUCaps() — DXGI VRAM detection
//   RestorePlatformDisplayMode() — ChangeDisplaySettingsExW restore
//   ReleasePlatformContext() — wglMakeCurrent(nullptr, nullptr)
//   LoadTextureFromFilePlatform(...) — GDI+ decode + GL upload
//   HasPlatformGLContext() / MakePlatformContextCurrent() / RebindPlatformContext() — WGL context
//   PresentPlatformFrame() — VSync + SwapBuffers
//   DrawIntroMovieFrame() — movie frame upload + logo + spacebar skip
//   ApplyGlyphColorAlpha(...) — file-static GDI glyph colour helper
//   SetPlatformLineSmoothing(bool) / SetPlatformMultisampling(bool) — GL_LINE_SMOOTH / GL_MULTISAMPLE on/off
//   SetupPlatformShadowTextureParams(GLenum, const float[4]) — CLAMP_TO_BORDER + border colour
//   SetPlatformDrawBufferNone() — glDrawBuffer(GL_NONE)

#endif // __USE_OPENGL__
