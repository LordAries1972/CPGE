# VulkanRenderer Class - Usage Guide

## Overview

`VulkanRenderer` is the Vulkan renderer (Windows: Win32 surface, Linux: XCB surface, Android: `ANativeWindow` surface). It owns the Vulkan instance, device, swapchain, render passes, the 2D overlay (Direct2D/WIC on Windows, CPU RGBA staging on Linux/Android) and the intro-movie blit path. The platform-agnostic Vulkan code lives in `VULKAN_Renderer.cpp` / `VULKAN_RenderFrame.cpp`; the platform-specific code lives in one file per platform (see below).

## 24 November 2026 - Per-Platform Renderer Split

The platform-specific Vulkan code that used to live behind `#if defined(PLATFORM_WINDOWS) / PLATFORM_LINUX / PLATFORM_ANDROID)` guards inside `VULKAN_Renderer.cpp` and `VULKAN_RenderFrame.cpp` now lives in **one file per platform** (Daniel's request — working on one platform means working in one file):

| File | Platform | What lives there |
|---|---|---|
| `windows-vulkan-render.h/.cpp` | Windows (Win32 + D2D/WIC) | `CreateSurface` (`vkCreateWin32SurfaceKHR`), `RecreateSurface`, fullscreen-exclusive acquire/release, `SetFullScreen` / `SetFullExclusive` / `SetWindowedScreen`, `EnforceFullscreenWindowPlacement`, the whole Direct2D/DWrite/WIC overlay block (`DrawRectangleD2D`, `DrawTextD2D`, `GetOrCreateTextFormat`, `InvalidateTextFormatCache`), `CreatePlatformOverlayTargets` / `CreatePlatformBackgroundTarget` / `CreatePlatformBackgroundOverlay` / `CopyPlatformOverlayToStaging` / `CopyPlatformBgOverlayToStaging`, `RenderPlatformOverlay` (scene overlays, D2D text, intro movie), `LoadTextureFromFilePlatform` (WIC decode), the 2D blit functions (`DrawRectangle`, `DrawCircle`, `DrawFilledPolygon`, `DrawCurve`, `DrawTexture`, `Blit2DObject*`, `Blit2DAtlasTile`, `Blit2DScrollingObjectToSize`, `Blit2DCenteredZoom`, `Blit2DColoredPixel`, `RenderBackgroundImage`), `RenderIntroMovie` |
| `linux-vulkan-render.h/.cpp` | Linux (XCB) | `CreateSurface` (`vkCreateXcbSurfaceKHR`), `RecreateSurface`, `LoadTextureFromFilePlatform` (stb_image), `RenderPlatformOverlay` (intro movie + GameTitle zoom/strobe + `fxManager.Render2D()` + `guiManager.Render()` + stale movie-frame clear, Phase C 25 Nov 2026), `RenderIntroMovie` (decoder pump + scaled movie frame → overlay staging, Phase C 25 Nov 2026), the native Vulkan 2D blits (`DrawTexture`, `Blit2DObject*`, `Blit2DAtlasTile`, `Blit2DScrollingObjectToSize`, `Blit2DCenteredZoom`, `Blit2DColoredPixel`, `ReplayQueued2DBlits`, Phase C 25 Nov 2026) |
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

## 25 November 2026 - Native Vulkan 2D blits on Linux (Phase C)

The eight `Blit2D*` entry points and `DrawTexture` are implemented natively
in `linux-vulkan-render.cpp` using the existing 2D pipeline (`m_2dPipeline` /
`Main2DPipeline()` for the MSAA variant) — no new pipeline and no shader
change. Because `Render2D()` is invoked from `RenderPlatformOverlay()`
before the main render pass begins (`VULKAN_RenderFrame.cpp:921-930`), the
blits cannot draw immediately: each call validates its arguments, converts
its source rect to float UVs from `m_textures2D[idx].width/height`, and
appends a quad to a per-frame queue (`LinuxBlitQuad` in `VULKAN_Renderer.h`,
`PLATFORM_LINUX` block). A replay step inside the main render pass
(`ReplayQueued2DBlits`, called from `VULKAN_RenderFrame.cpp` in the
background slot and again after the overlay composite) uploads the queued
quads as 4-vertex triangle-strip quads into a per-frame-in-flight
host-visible vertex buffer (same `VkVertex2D` layout as the unit quad;
positions stay 0..1, UVs are baked per vertex) and draws them, one
descriptor set per distinct texture per frame (allocated from
`m_descriptorPool`, freed via `fd.pendingFreeSets`, as the
background/overlay quads do). Push constants are the existing 7 floats
(screen size, dest rect, opacity 1.0); tint/alpha travels in the vertex
colour. The 2D pipeline is a triangle strip with per-quad push constants,
so each queued quad is its own `vkCmdDraw(4)` — a single-draw batched
triangle-list pipeline is a follow-up if draw-call overhead ever matters.

Behaviour mirrors the Windows D2D reference (`windows-vulkan-render.cpp`)
and the OpenGL float-UV reference (`OpenGLRenderer::Render2DQuadUV`): zoom
factor clamped to 0–0.75 with a float centre crop (never rounded, so the
zoom glides without 1-pixel stutter); scrolling is a two-part wrapped
split; atlas tiles are row-major with the out-of-range guard
(`tileRow >= tilesPerCol` returns). `Blit2DColoredPixel` draws against the
1×1 white `m_defaultTexture` with the pixel colour in the vertex colour.
`m_drawToBackground` routes a blit to the background queue (replayed
before 3D, e.g. the zoomed `IMG_GAMEINTRO1`) or the overlay queue
(replayed after 3D, before the fullscreen fade), preserving the Windows
draw order. Queue capacity is 16384 quads per frame per queue (re-checked
against the real callers: 16 px tiles at 1920×1080 need ~8,540 quads,
starfield + fireworks ~1,000 more); overflow drops extras with a throttled
warning. The Linux `RenderPlatformOverlay` also mirrors the Windows one
for the callers that drive the blits: the intro movie (see below), the
GameTitle background zoom, the TSOO fade strobe, and `Render2D` routed to
the background queue on the loaded GameTitle screen so starfield/fireworks
sit behind the 3D models. Android still uses the no-op stubs in
`android-vulkan-render.cpp` — same design ports 1:1 as a follow-up; the
Windows and Android `ReplayQueued2DBlits` bodies are empty no-ops, and the
two one-line calls in shared `VULKAN_RenderFrame.cpp` are the only shared
change (accepted as the minimal honest design — the alternatives replay at
the wrong time/order). Windows behaviour is unchanged.

Example — tilemap, zoom and fade-strobe call sites are unchanged; they call
the portable API and the Linux path now honours them:

```cpp
// Tilemap scroller (FXManager::RenderTileMapScroller): one call per visible tile.
renderer->Blit2DAtlasTile(atlasIndex, tileIndex, tileW, tileH, destX, destY);
// Centred zoom (FXManager::RenderZoomedImage): zoomFactor 0.0–0.75.
renderer->Blit2DCenteredZoom(imgID, destX, destY, destW, destH, zoomLevel);
// Image fade strobe (FXManager::RenderImageFadeStrobe): alpha 0.0–1.0.
renderer->Blit2DObjectToSizeWithAlpha(type, x, y, w, h, alpha);
```

### 25 November 2026 - Intro movie on Linux/Vulkan: pump, scale, upload (Phase C)

The intro movie could never display on Linux/Vulkan — three stacked
omissions, all in `linux-vulkan-render.cpp`, all fixed the same day:

1. **Never pumped.** `moviePlayer.UpdateFrame()` was never called on the
   Linux/Android Vulkan path, so the FFmpeg decoder never advanced, never
   reached EOF, `IsPlaying()` stayed true forever, and the main-loop
   transition out of `SCENE_INTRO_MOVIE` (`main.cpp`, requires
   `!moviePlayer.IsPlaying()`) never fired — the Phase C 300 s run sat in
   the movie scene for its whole duration while OpenGL transitioned in
   45 s. `RenderIntroMovie()` now calls `moviePlayer.UpdateFrame()` first,
   mirroring Windows Vulkan / OpenGL. (The same one line was added to
   `android-vulkan-render.cpp`; Android's movie path is otherwise still
   dead — never called, no upload — as a follow-up.)
2. **Dimension gate (latent, kept as a safety net).** `RenderIntroMovie()` uploaded the decoded frame
   ONLY when it exactly matched the overlay size, so a 1280×720 movie on
   an 800×600 overlay would have dropped every frame. It now scales the decoded BGRA
   frame into the overlay staging buffer (nearest-neighbour; the exact
   match stays a fast `memcpy` path). A one-time log line records the
   frame vs overlay size on the first movie frame. Correction: this gate
   was NOT the active blocker — the FFmpeg scaler already targets the
   config resolution, so decoded frames arrive 800×600 (proven by the run
   log `RenderIntroMovie: frame 800x600, overlay 800x600`); the active
   blockers were (1) and (3). The scale stays as a safety net for a
   config change or a null renderer delivering native-size frames.
3. **Never uploaded, never called.** The Linux `RenderIntroMovie()` was
   never invoked (the shared `VULKAN_RenderFrame.cpp` has no
   `SCENE_INTRO_MOVIE` case and the Linux `RenderPlatformOverlay` did not
   call it) and the Linux `UploadPlatformOverlays()` was a no-op, so even
   a staged frame could never reach the GPU. The Linux
   `RenderPlatformOverlay` now calls `RenderIntroMovie()` for
   `SCENE_INTRO` / `SCENE_INTRO_MOVIE` (mirroring Windows) and
   `UploadPlatformOverlays()` now calls the shared `UploadOverlayToVulkan`
   / `UploadBgOverlayToVulkan`, which early-return when nothing is dirty.
4. **Stale frame covered the next scene (found during verification).**
   Once the movie played, its last frame (opaque, alpha 255) stayed in
   the overlay texture, and the shared frame draws that texture
   fullscreen every frame — on Windows the D2D overlay is re-cleared to
   transparent each frame, but on Linux nothing rewrites the staging
   buffer. The GameTitle background matched the movie's last frame at
   56% bright-pixel overlap and was static across captures, while the
   zoomed background quad was proven enqueued and replayed, so the stale
   frame — not the blits — was covering the background and the 3D scene.
   `RenderPlatformOverlay` now clears the staging buffer to transparent
   black once, on the first frame after the movie stops (paused gameplay
   playback keeps its frame), and `UploadPlatformOverlays()` uploads it.
   The clear lives in `RenderPlatformOverlay`, not `RenderIntroMovie`:
   that function is only called during movie scenes, and a clear placed
   there never runs once the scene has changed to GameTitle (proven by a
   first attempt that left the stale frame in place). After the fix the
   t=75 s GameTitle capture shows the full title screen — zoomed
   `gameintro1.png` background (sun, ringed planet, Earth), the 3D ship,
   TSOO logo and title, GAME OPTIONS menu and starfield.

### 25 November 2026 - Shutdown validation leak fixed (Phase C)

Validation layers reported 2 errors on every clean shutdown
(`VUID-vkDestroyDevice-device-05137`): the overlay staging buffer
(`m_overlayStagingBuffer`) and its memory were never destroyed in
`Cleanup()` — `Resize()` destroyed them, and Windows destroyed them in
`ReleasePlatformOverlayResources()`, but on Linux/Android that function
was a no-op. `Cleanup()` (`VULKAN_Renderer.cpp`) now destroys both
staging pairs after `ReleasePlatformOverlayResources()`; on Windows the
handles are already null there, so the guards skip. (This also resolves
the "possible pre-existing Linux staging-buffer leak" note below.)

**Left in place, intentionally:**

- `CreateSwapChain`'s Windows-only `pNext` block (`VkSurfaceFullScreenExclusiveInfoEXT` + `VkSurfaceFullScreenExclusiveWin32InfoEXT` stack locals) stays in `VULKAN_Renderer.cpp` — the structs' lifetime must span the `vkCreateSwapchainKHR` call, so the block cannot be factored into a helper without changing its lifetime.
- The file-top Windows includes (`d2d1.h`, `dwrite.h`, `wincodec.h`) and the `DirectXMath`-vs-glm branches in `VULKAN_RenderFrame.cpp` (math library selection, not platform behaviour).
- The `stb_image` include at the top of `VULKAN_Renderer.cpp` (now unused there — `LoadTexture`'s stb branch moved to the Linux/Android files, which carry their own stb include).
- The `CreateSwapChain` swapchain-present-mode / vsync helper is `GetPlatformVSyncEnabled()` (factored); the all-three-platforms guard in `VULKAN_RenderFrame.cpp` covers shared code on every platform.

**Pre-existing notes (verbatim moves — behaviour unchanged):**

- `BlitTextureD2D` (above) is a dead declaration.
- Vulkan `Cleanup` destroys the overlay staging buffers only on Windows, but `Resize` destroys them on every platform — a possible pre-existing Linux staging-buffer leak, preserved verbatim by the move. **Fixed 25 November 2026 (Phase C)** — `Cleanup()` now destroys both staging pairs itself (see the 25 November 2026 section above); validation-clean shutdown confirmed, 0 messages over 4 clean-shutdown runs (the fourth on the final binary).
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
