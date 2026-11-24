// -------------------------------------------------------------------------------------------------------------
// windows-vulkan-render.h — Windows Vulkan implementation
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
// Windows-only: Direct2D / DirectWrite / WIC (overlay + text rendering).
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

// -------------------------------------------------------------------------------------------------------------
// Members defined in the matching .cpp (declared in the shared class header):
// -------------------------------------------------------------------------------------------------------------
//   CreateSurface(NativeWindowHandle) — vkCreateWin32SurfaceKHR
//   RecreateSurface() — destroy + recreate the Win32 surface
//   EnforceFullscreenWindowPlacement(...) — Windows fullscreen window placement
//   ReleaseFullScreenExclusiveIfActive() / AcquireFullScreenExclusiveIfAvailable() — VK_EXT_full_screen_exclusive
//   SetFullScreen() / SetFullExclusive(uint32_t, uint32_t) / SetWindowedScreen() — Windows display modes
//   DrawRectangle(...) / DrawCircle(...) / DrawFilledPolygon(...) / DrawFilledPolygonBatch(...) / DrawCurve(...) — D2D shapes
//   PushClipRect(...) / PopClipRect() — D2D clip rects
//   DrawRectangleD2D(...) / DrawTextD2D(...) / GetOrCreateTextFormat(...) / InvalidateTextFormatCache() — D2D helpers
//   DrawMyText(...) x2 / DrawMyTextCentered(...) / DrawMyTextWithFont(...) / DrawMyTextStyled(...) — D2D text
//   DrawTexture(...) / Blit2DObject(...) / Blit2DObjectToSize(...) / Blit2DObjectToSizeWithAlpha(...) / Blit2DObjectAtOffset(...) / Blit2DAtlasTile(...) / Blit2DScrollingObjectToSize(...) / Blit2DCenteredZoom(...) — D2D blits
//   Blit2DColoredPixel(int, int, float, XMFLOAT4) — D2D coloured pixel
//   RenderBackgroundImage() — D2D background image
//   InitPlatformDetails() / InitPlatformWindow(...) — osDetails + window handles
//   AppendPlatformSurfaceExtension(...) / ProbePlatformInstanceExtensions(...) / AppendPlatformDeviceExtensions(...) — instance/device extensions
//   GetPlatformVSyncEnabled() / HidePlatformMouseCursor() / RestorePlatformDisplayMode()
//   ReleasePlatformOverlayResources() / ReleasePlatformD2DTextures() / ReleasePlatformD2DTexture(int) / ResetPlatformOverlayTargets()
//   CreatePlatformOverlayTargets(...) / CreatePlatformBackgroundTarget(...) / CreatePlatformBackgroundOverlay(...)
//   CopyPlatformOverlayToStaging() / CopyPlatformBgOverlayToStaging()
//   LoadTextureFromFilePlatform(...) — WIC decode + D2D bitmap
//   HasPlatformOverlayTarget() / BeginPlatformOverlayDraw() / RenderPlatformOverlay(...) / UploadPlatformOverlays(...) / CompositePlatformBackgroundOverlay(...)
//   RenderIntroMovie() — D2D bitmap video frame

#endif // __USE_VULKAN__
