// -------------------------------------------------------------------------------------------------------------
// android-vulkan-render.h — Android Vulkan implementation
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

#if defined(__USE_VULKAN__)
#include "VULKAN_Renderer.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "WinSystem.h"
#include "ThreadManager.h"
#include "Configuration.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "GUIManager.h"
#include "FXManager.h"
#include "MoviePlayer.h"
#include "ScreenRecorder.h"
#include "KeyboardHandler.h"
// stb_image declarations for LoadTextureFromFilePlatform (Linux/Android branch).
// The implementation lives in VulkanModels.cpp (which defines
// STB_IMAGE_IMPLEMENTATION) — defining it here too would duplicate every stb_*
// symbol at link time. Must be at file scope.
#if __has_include("stb_image.h")
    #include "stb_image.h"
#endif

// -------------------------------------------------------------------------------------------------------------
// Members defined in the matching .cpp (declared in the shared class header):
// -------------------------------------------------------------------------------------------------------------
//   CreateSurface(NativeWindowHandle) — vkCreateAndroidSurfaceKHR
//   RecreateSurface() — destroy the old surface, refresh the window
//   from AndroidApp (it changes across TERM_WINDOW / INIT_WINDOW), and
//   recreate via CreateSurface (Phase D step 3)
//   SetFullScreen() / SetFullExclusive(uint32_t, uint32_t) / SetWindowedScreen() — return true / Resize (handled by the platform windowing system)
//   DrawRectangle(...) / DrawCircle(...) / DrawFilledPolygon(...) / DrawFilledPolygonBatch(...) / DrawCurve(...) — void stubs (Phase C)
//   PushClipRect(...) / PopClipRect() — void stubs
//   DrawMyText(...) x2 / DrawMyTextCentered(...) / DrawMyTextWithFont(...) / DrawMyTextStyled(...) — void stubs
//   DrawTexture(...) / Blit2DObject(...) / Blit2DObjectToSize(...) / Blit2DObjectToSizeWithAlpha(...) / Blit2DObjectAtOffset(...) / Blit2DAtlasTile(...) / Blit2DScrollingObjectToSize(...) / Blit2DCenteredZoom(...) — void stubs / fallbacks
//   Blit2DColoredPixel(int, int, float, const Vector4&) — no-op stub
//   RenderBackgroundImage() — comment-only stub
//   InitPlatformDetails() / InitPlatformWindow(...) — osDetails + ANativeWindow
//   AppendPlatformSurfaceExtension(...) — VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
//   ProbePlatformInstanceExtensions(...) / AppendPlatformDeviceExtensions(...) — no-op
//   GetPlatformVSyncEnabled() — returns true
//   HidePlatformMouseCursor() / RestorePlatformDisplayMode() / ReleasePlatformOverlayResources() / ReleasePlatformD2DTextures() / ReleasePlatformD2DTexture(int) / ResetPlatformOverlayTargets() — no-op
//   CreatePlatformOverlayTargets(...) — returns true; CreatePlatformBackgroundTarget(...) / CreatePlatformBackgroundOverlay(...) — no-op
//   CopyPlatformOverlayToStaging() / CopyPlatformBgOverlayToStaging() — no-op (staging buffer already holds CPU RGBA)
//   LoadTextureFromFilePlatform(...) — stb_image decode + upload
//   HasPlatformOverlayTarget() — returns true; BeginPlatformOverlayDraw() — no-op
//   RenderPlatformOverlay(...) — fxManager.Render2D() + guiManager.Render()
//   UploadPlatformOverlays(...) / CompositePlatformBackgroundOverlay(...) — no-op
//   RenderIntroMovie() — staging-buffer movie frame copy

#endif // __USE_VULKAN__
