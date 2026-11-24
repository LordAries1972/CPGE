# VulkanRenderer Class - Usage Guide

## Overview

`VulkanRenderer` is the Vulkan renderer (Windows: Win32 surface, Linux: XCB surface, Android: `ANativeWindow` surface). It owns the Vulkan instance, device, swapchain, render passes, the 2D overlay (Direct2D/WIC on Windows, CPU RGBA staging on Linux/Android) and the intro-movie blit path. The platform-agnostic Vulkan code lives in `VULKAN_Renderer.cpp` / `VULKAN_RenderFrame.cpp`; the platform-specific code lives in one file per platform (see below).

## 24 November 2026 - Per-Platform Renderer Split

The platform-specific Vulkan code that used to live behind `#if defined(PLATFORM_WINDOWS) / PLATFORM_LINUX / PLATFORM_ANDROID)` guards inside `VULKAN_Renderer.cpp` and `VULKAN_RenderFrame.cpp` now lives in **one file per platform** (Daniel's request — working on one platform means working in one file):

| File | Platform | What lives there |
|---|---|---|
| `windows-vulkan-render.h/.cpp` | Windows (Win32 + D2D/WIC) | `CreateSurface` (`vkCreateWin32SurfaceKHR`), `RecreateSurface`, fullscreen-exclusive acquire/release, `SetFullScreen` / `SetFullExclusive` / `SetWindowedScreen`, `EnforceFullscreenWindowPlacement`, the whole Direct2D/DWrite/WIC overlay block (`DrawRectangleD2D`, `DrawTextD2D`, `GetOrCreateTextFormat`, `InvalidateTextFormatCache`), `CreatePlatformOverlayTargets` / `CreatePlatformBackgroundTarget` / `CreatePlatformBackgroundOverlay` / `CopyPlatformOverlayToStaging` / `CopyPlatformBgOverlayToStaging`, `RenderPlatformOverlay` (scene overlays, D2D text, intro movie), `LoadTextureFromFilePlatform` (WIC decode), the 2D blit functions (`DrawRectangle`, `DrawCircle`, `DrawFilledPolygon`, `DrawCurve`, `DrawTexture`, `Blit2DObject*`, `Blit2DAtlasTile`, `Blit2DScrollingObjectToSize`, `Blit2DCenteredZoom`, `Blit2DColoredPixel`, `RenderBackgroundImage`), `RenderIntroMovie` |
| `linux-vulkan-render.h/.cpp` | Linux (XCB) | `CreateSurface` (`vkCreateXcbSurfaceKHR`), `RecreateSurface`, `LoadTextureFromFilePlatform` (stb_image), `RenderPlatformOverlay` (2-line stub: `fxManager.Render2D()` + `guiManager.Render()`), `RenderIntroMovie` (movie frame → overlay staging) |
| `android-vulkan-render.h/.cpp` | Android (`ANativeWindow`) | `CreateSurface` (`vkCreateAndroidSurfaceKHR`), `LoadTextureFromFilePlatform` (stb_image), `RenderPlatformOverlay` (same 2-line stub as Linux), `RenderIntroMovie` |

**What stays shared:** all platform-agnostic Vulkan code stays in `VULKAN_Renderer.cpp` / `VULKAN_RenderFrame.cpp` (instance/device/swapchain creation, render passes, command buffers, `RenderFrame`, `CreateSwapChain` — except the Windows-only `pNext` fullscreen-exclusive chain, see below). Member state stays declared in `VULKAN_Renderer.h` (already guarded per platform: D2D/WIC members Windows-only, XCB/Linux members, `ANativeWindow` Android).

**How it works:** whole platform-dispatched member functions moved verbatim (one definition per platform file — only one is compiled per platform). Small branches inside shared functions were factored into small platform helpers (e.g. `InitPlatformDetails()`, `InitPlatformWindow(hwnd, hInstance)`, `AppendPlatformSurfaceExtension()`, `ProbePlatformInstanceExtensions()`, `AppendPlatformDeviceExtensions()`, `GetPlatformVSyncEnabled()`, `HidePlatformMouseCursor()`, `RestorePlatformDisplayMode()`, `ReleasePlatformOverlayResources()`, `ReleasePlatformD2DTextures()`, `ReleasePlatformD2DTexture(textureId)`, `ResetPlatformOverlayTargets()`, `CreatePlatformOverlayTargets(width, height)`, `CreatePlatformBackgroundTarget(...)`, `CreatePlatformBackgroundOverlay(...)`, `CopyPlatformOverlayToStaging(...)`, `CopyPlatformBgOverlayToStaging(...)`, `LoadTextureFromFilePlatform(filename, is2D, textureId, fullPath)`, `HasPlatformOverlayTarget()`, `BeginPlatformOverlayDraw()`, `RenderPlatformOverlay(cmd)`, `UploadPlatformOverlays(cmd)`, `CompositePlatformBackgroundOverlay(cmd, fd)`, `RenderIntroMovie()`), declared in the class header's private section (C++ requires member declarations in the class definition) and defined once per platform file; the shared function calls the helper. Inside the new platform files **no platform guards are needed** — the file itself is the platform; the build system is the selector.

Two notes on the factoring:

- `Blit2DColoredPixel` has different signatures per platform by design: Windows takes an `XMFLOAT4` colour, Linux/Android take a `const Vector4&`. Each platform file defines its own signature; the declaration in `VULKAN_Renderer.h` stays platform-guarded as before.
- `BlitTextureD2D` is declared in `VULKAN_Renderer.h` (Windows-only section) but **never defined anywhere** — a pre-existing dead declaration. It was not moved and not removed; it is noted here so it is not mistaken for a missing definition.

**How the build selects files:**

- Linux (`Linux/CMakeLists.txt`, Vulkan block): `if(ANDROID)` compiles `android-vulkan-render.cpp`, otherwise `linux-vulkan-render.cpp`.
- Windows (`CrossPlatformGameEngine.vcxproj` + `.filters`): `windows-vulkan-render.cpp` is a `ClCompile` entry and `windows-vulkan-render.h` a `ClInclude` entry, next to `VULKAN_Renderer.cpp/.h`.

**How to work on one platform:** edit only that platform's file. A change to the Linux XCB surface path, for example, touches only `linux-vulkan-render.cpp` and cannot affect Windows or Android. When adding a new platform-specific function, add the declaration to `VULKAN_Renderer.h` (private section, grouped with the other platform helpers) and add one definition to each of the three platform files (a no-op `return true;` / `return;` body is fine where the platform has nothing to do).

**Left in place, intentionally:**

- `CreateSwapChain`'s Windows-only `pNext` block (`VkSurfaceFullScreenExclusiveInfoEXT` + `VkSurfaceFullScreenExclusiveWin32InfoEXT` stack locals) stays in `VULKAN_Renderer.cpp` — the structs' lifetime must span the `vkCreateSwapchainKHR` call, so the block cannot be factored into a helper without changing its lifetime.
- The file-top Windows includes (`d2d1.h`, `dwrite.h`, `wincodec.h`) and the `DirectXMath`-vs-glm branches in `VULKAN_RenderFrame.cpp` (math library selection, not platform behaviour).
- The `stb_image` include at the top of `VULKAN_Renderer.cpp` (now unused there — `LoadTexture`'s stb branch moved to the Linux/Android files, which carry their own stb include).
- The `CreateSwapChain` swapchain-present-mode / vsync helper is `GetPlatformVSyncEnabled()` (factored); the all-three-platforms guard in `VULKAN_RenderFrame.cpp` covers shared code on every platform.

**Pre-existing notes (verbatim moves — behaviour unchanged):**

- `BlitTextureD2D` (above) is a dead declaration.
- Vulkan `Cleanup` destroys the overlay staging buffers only on Windows, but `Resize` destroys them on every platform — a possible pre-existing Linux staging-buffer leak, preserved verbatim by the move.
- Vulkan `RecreateSurface` has no Android branch (Android falls through to just the log line). **Fixed 24 November 2026 (Phase D step 3)** — see the section below.

**Verification (24 November 2026):** `./cmake-build.sh all debug -clean` → `PASSED: opengl vulkan`, 0 errors. The OpenGL renderer was split the same day — see `Docs/OpenGLRenderer-Example-Usage.md` (24 November 2026 - Per-Platform Renderer Split section).

## 24 November 2026 - RecreateSurface Android branch (Phase D step 3)

`android-vulkan-render.cpp` gained a real `RecreateSurface()`, mirroring
the Linux branch: refresh the window from `AndroidApp::GetNativeWindow()`
(the glue hands out a new `ANativeWindow` across `TERM_WINDOW` /
`INIT_WINDOW`, so the cached pointer can be stale), destroy the old
surface if there is one, then `CreateSurface()` with the current window.
With no window at all it logs `LOG_ERROR` and throws `std::runtime_error`
(the shared `RecreateSwapChain` caller retries once on a "Surface lost"
error and lets anything else propagate, exactly as on Linux). The
`android-vulkan-render.h` doc-list line for it was updated at the same
time. Verified in the Vulkan Android build
(`scope/android-vulkan-build-10.log`, exit 0); surface loss itself can
only be exercised on a device, so the path is compile/link verified
only.
