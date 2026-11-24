# OpenGLRenderer Class - Usage Guide

## Overview

`OpenGLRenderer` is the OpenGL 3.3 core-profile renderer (Windows: WGL,
Linux: GLX, Android: EGL in Phase D). It owns the window surface, the shader
programs, the model/texture caches, and the reflection-probe and lighting
passes. Texture uploads happen on the loader thread (`IOLoaderThread.cpp`,
`LoaderTaskThread`), which needs a current GL context of its own that shares
objects with the main rendering context.

## Basic Usage

```cpp
#include "OpenGLRenderer.h"

// Create the main context (platform-specific: WGL on Windows, GLX on Linux):
renderer->CreateOpenGLContext(hwnd);

// Load a texture through the loader thread (shared context required):
renderer->LoadTexture(textureId, L"Assets/texture.png", false);

// Per-frame rendering runs on the render thread:
renderer->RenderFrame();
```

## Loader Context

The loader thread uploads textures with the same GL objects as the render
thread, so it needs a *shared* context: a second context created with the
main context as its share context. On Windows this is `m_loaderGLContext`
(`HGLRC`); on Linux it is the GLX mirror of the same member. The loader
thread makes it current before `LoadAllKnownTextures()` / GLTF texture
uploads and releases it afterwards; it is destroyed in
`CleanupPlatformSpecificContext()` before the main context.

## 24 November 2026 - Shared GLX Loader Context (Linux)

**What changed:** before this fix the loader context was WGL-only
(`OpenGLRenderer.h:618`, `HGLRC m_loaderGLContext`). On Linux the loader
thread had *no* GL context, so every texture upload failed and the engine
loaded 0 of 21 textures, with the misleading message
`[OpenGLModels] Cannot open texture file` even though the files opened
fine.

The member is now platform-typed in `OpenGLRenderer.h`, mirroring the
`OpenGLContext` struct pattern:

```cpp
#if defined(PLATFORM_WINDOWS)
    HGLRC m_loaderGLContext = nullptr;      // Shared WGL context for loader-thread texture uploads
#elif defined(PLATFORM_LINUX)
    GLXContext m_loaderGLContext = nullptr; // Shared GLX context for loader-thread texture uploads
#endif
// No member on Android: the EGL loader context is Phase D work.
```

`CreateOpenGLContext` (`OpenGLRenderer.cpp`, Linux branch) creates the
shared loader context after the main GLX context, with the same 3.3 core
attribs (falling back to a legacy `glXCreateContext` that shares with the
main context), and logs `[OpenGLRenderer] Shared loader GLX context
created`. The `glXMakeCurrent` failure path also destroys the loader
context. `CleanupPlatformSpecificContext` destroys the loader context
before the main context; its guards were converted from raw `_WIN32` /
`__linux__` to `PLATFORM_WINDOWS` / `PLATFORM_LINUX` /
`PLATFORM_ANDROID` (which also fixes Android taking the GLX branch, since
Android defines `__linux__`).

Two supporting changes:

1. `XInitThreads()` is now called at the top of `CreateLinuxWindow()`
   (`main.cpp`, `PLATFORM_LINUX`-guarded, before the first `XOpenDisplay`).
   It is required because the loader thread now shares the renderer's X11
   `Display` with the render thread. It was not called anywhere in the
   codebase before.
2. `LoadTexture` (`OpenGLRenderer.cpp`) now distinguishes the two failure
   modes instead of always blaming stb_image:
   - `cannot open texture file` (checked with non-throwing
     `std::filesystem::exists`), and
   - `GL texture upload failed (no current GL context on this thread?)`.

**How to use it:** nothing changes for callers. The loader thread acquires
the context in `LoaderTaskThread` (`IOLoaderThread.cpp`, Linux branch):

```cpp
// IOLoaderThread.cpp - LoaderTaskThread, Linux mirror of the Windows block:
#elif defined(__USE_OPENGL__) && defined(PLATFORM_LINUX)
    glXMakeCurrent(display, window, m_loaderGLContext);
    // ... LoadAllKnownTextures() / GLTF texture uploads ...
    glFlush();
    glXMakeCurrent(display, None, nullptr);
#endif
```

**Verification (24 November 2026, Linux OpenGL under Xvfb):**
`[OpenGLRenderer] Shared loader GLX context created` is logged at startup
and `LoadAllKnownTextures: 21 loaded, 0 failed` (was 0 loaded, 21 failed).
The Vulkan renderer does not share this path - its texture path was
verified independently.

## 24 November 2026 - Per-Platform Renderer Split

The platform-specific OpenGL code that used to live behind `#if defined(PLATFORM_WINDOWS) / PLATFORM_LINUX / PLATFORM_ANDROID)` guards inside `OpenGLRenderer.cpp` and `OpenGLRenderFrame.cpp` now lives in **one file per platform** (Daniel's request — working on one platform means working in one file):

| File | Platform | What lives there |
|---|---|---|
| `windows-opengl-render.h/.cpp` | Windows (WGL) | `CreateOpenGLContext`, `InitializeOpenGLExtensions` (glewInit), GDI+ text backend (`InitPlatformTextBackend` / `ShutdownPlatformTextBackend`), `FindMultisamplePixelFormat`, `DetectPlatformGPUCaps` (DXGI VRAM), `RenderTextToTexture` / `RenderTextCenteredToTexture` / `DrawMyTextStyled`, fullscreen mode functions, `DrawIntroMovieFrame` |
| `linux-opengl-render.h/.cpp` | Linux (GLX) | `CreateOpenGLContext` (GLX), shared loader GLX context creation, `LoadTextureFromFilePlatform` (stb_image), `HasPlatformGLContext` / `MakePlatformContextCurrent` / `RebindPlatformContext` / `PresentPlatformFrame` (`glXSwapBuffers`) |
| `android-opengl-render.h/.cpp` | Android (EGL/GLES3) | `CreateOpenGLContext` (real EGL path since Phase D step 2 — ES 3.1 context, shared loader EGL context + 1x1 pbuffer surface), `InitializeOpenGLExtensions` (no-op — GLES3 needs no glewInit), `LoadTextureFromFilePlatform` (stb_image), present via `eglSwapBuffers` |

**What stays shared:** all platform-agnostic rendering stays in `OpenGLRenderer.cpp` / `OpenGLRenderFrame.cpp` (shader programs, model/texture caches, reflection-probe and lighting passes, `RenderFrame`). Member state stays declared in `OpenGLRenderer.h` (already guarded per platform: `HDC`/`HGLRC` Windows, `Display*`/`GLXContext` Linux, `EGLDisplay`/`EGLContext`/`EGLSurface` Android, `m_loaderGLContext` per platform).

**How it works:** whole platform-dispatched member functions moved verbatim (one definition per platform file — only one is compiled per platform). Small branches inside shared functions were factored into small platform helpers (e.g. `HasPlatformGLContext()`, `MakePlatformContextCurrent()`, `RebindPlatformContext()`, `PresentPlatformFrame()`, `LoadTextureFromFilePlatform()`, `InitPlatformTextBackend()`, `DetectPlatformGPUCaps()`), declared in the class header's private section (C++ requires member declarations in the class definition) and defined once per platform file; the shared function calls the helper. Inside the new platform files **no platform guards are needed** — the file itself is the platform; the build system is the selector.

**How the build selects files:**

- Linux (`Linux/CMakeLists.txt`, OpenGL block): `if(ANDROID)` compiles `android-opengl-render.cpp`, otherwise `linux-opengl-render.cpp`.
- Windows (`CrossPlatformGameEngine.vcxproj` + `.filters`): `windows-opengl-render.cpp` is a `ClCompile` entry and `windows-opengl-render.h` a `ClInclude` entry, next to `OpenGLRenderer.cpp/.h`.

**How to work on one platform:** edit only that platform's file. A change to the Linux GLX context path, for example, touches only `linux-opengl-render.cpp` and cannot affect Windows or Android. When adding a new platform-specific function, add the declaration to `OpenGLRenderer.h` (private section, grouped with the other platform helpers) and add one definition to each of the three platform files (a no-op `return true;` / `return;` body is fine where the platform has nothing to do).

**Left in place, intentionally:** the top-of-file `#pragma comment(lib, ...)` lines and gdiplus.h/dxgi.h includes in `OpenGLRenderer.cpp` (build scaffolding); the `#if defined(_WIN64)` MemoryCopy-vs-memcpy choice in `OpenGLRenderFrame.cpp` (word size, not platform); the debug FPS-text and renderer-info overlay blocks in `OpenGLRenderFrame.cpp` (inline debug-only display blocks using frame locals — factoring them would need a wide parameter list).

**Verification (24 November 2026):** `./cmake-build.sh all debug -clean` → `PASSED: opengl vulkan`, 0 errors. The Vulkan renderer was split the same day — see `Docs/VulkanRenderer-Example-Usage.md`.

## 24 November 2026 - Android EGL context + GLES compatibility (Phase D step 2)

The Android OpenGL path is now real: `android-opengl-render.cpp::CreateOpenGLContext`
replaces the stub the split left behind, and the OpenGL Android build links
with zero errors (`android-opengl-build-8.log`, exit 0).

### EGL context creation (`CreateOpenGLContext`)

Mirrors the GLX path (`linux-opengl-render.cpp`): the `ANativeWindow*`
arrives as a `void*` (`NativeWindowHandle` on Android — see `Renderer.h`),
EGL uses the default display, and the config is RGBA8 + 24-bit depth +
8-bit stencil + window surface, with 4x MSAA samples when the Video
settings ask for it (`antiAliasingEnabled && msaaEnabled`). The context is
**GLES 3.1** (compute shaders need 3.1; the texture-size query uses the
`gl31.h` enums), falling back to **3.0** when 3.1 is unavailable. VSync is
`eglSwapInterval(display, enableVSync ? 1 : 0)` in
`SetupPlatformSpecificContext()` (called right after context creation, with
the context current — same placement as the GLX swap control).
`ReleasePlatformContext()` unbinds the context so the render thread can
take it over (`MakePlatformContextCurrent()`); `CleanupPlatformSpecificContext()`
destroys the loader context first, then the main context/surface/display
(Linux mirror). Every failure logs `LOG_CRITICAL` and returns `false`, with
partial state unwound (surface/contexts destroyed, display terminated) —
no half-created context is left behind.

```cpp
// android-opengl-render.cpp — what CreateOpenGLContext does, in order:
ANativeWindow* window = static_cast<ANativeWindow*>(hWnd);   // must be non-null
m_glContext.eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
eglInitialize(display, &major, &minor);
eglChooseConfig(display, attribs, &config, 1, &numConfigs);  // RGBA8/depth24/stencil8, MSAA from settings
m_glContext.eglContext = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttribs31);  // or 3.0 on failure
m_loaderGLContext      = eglCreateContext(display, config, m_glContext.eglContext, ctxAttribs);  // shared objects
m_glContext.eglLoaderSurface = eglCreatePbufferSurface(display, config, { 1, 1 });  // drawable for the loader context
m_glContext.eglSurface = eglCreateWindowSurface(display, config, window, nullptr);
eglMakeCurrent(display, surface, surface, context);
```

### Shared EGL loader context (Android mirror of the WGL/GLX one)

`OpenGLRenderer.h` gained an Android `EGLContext m_loaderGLContext =
EGL_NO_CONTEXT` arm, and `OpenGLContext` gained an `EGLSurface
eglLoaderSurface` field. The loader context shares GL objects with the main
context, so the loader thread can upload textures — the same bug class as
the Linux Phase C fix (21/21 textures failed to load with no loader
context). The loader context is made current on the **1x1 pbuffer surface**,
not the window surface (the render thread owns that): a pbuffer is core EGL
and works on every device, unlike a surfaceless current context, which
needs `EGL_KHR_surfaceless_context`. `IOLoaderThread.cpp` gained the
Android acquire/release arms (mirror of the Windows/Linux ones); a failed
acquire logs `LOG_ERROR` — the failure is diagnosable, not silent.

### GLES compatibility — four platform helpers

Four desktop GL calls do not exist in OpenGL ES at all, so the shared code
now calls small platform helpers (declared in `OpenGLRenderer.h`'s private
section, defined once per platform file — the split's own pattern) instead
of the GL calls directly. Windows/Linux keep the desktop calls verbatim;
Android gets the ES behaviour:

| Helper | Desktop (Windows/Linux) | Android (ES) |
|---|---|---|
| `SetPlatformLineSmoothing(bool)` | `glEnable/glDisable(GL_LINE_SMOOTH)` | no-op (ES has no line smoothing; lines are aliased) |
| `SetPlatformMultisampling(bool)` | `glEnable/glDisable(GL_MULTISAMPLE)` | no-op (MSAA is an EGL config property) |
| `SetupPlatformShadowTextureParams(target, border)` | `CLAMP_TO_BORDER` + border colour (1.0 = lit) | `CLAMP_TO_EDGE` (border clamp is ES 3.2 only; the context is 3.1 — out-of-range shadow lookups read the edge texel instead) |
| `SetPlatformDrawBufferNone()` | `glDrawBuffer(GL_NONE)` | `glDrawBuffers(1, {GL_NONE})` (ES 3.0) |

Call sites (shared `OpenGLRenderer.cpp`, no platform guards added):
`SetupRenderStates()` (line smoothing + multisample), `DrawFilledPolygon()`
(line smoothing around the outline pass), `CreateShadowResourcesGL()`
(shadow wrap/border on both the 2D and 2D-array depth maps, draw-buffer
none on the depth-only FBO).

Two more GLES header facts, fixed without helpers because the functionality
exists in ES — only the header was missing:

- `GL_TEXTURE_WIDTH` / `GL_TEXTURE_HEIGHT` are ES 3.1 (`GLES3/gl31.h`), not
  ES 3.0 (`gl3.h`). `OpenGLRenderer.h`'s Android arm now includes `gl31.h`,
  so `Render2DQuad()`'s texture-size query and the Android texture loader's
  size query compile unchanged.
- The anisotropy enums (`GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT` /
  `GL_TEXTURE_MAX_ANISOTROPY_EXT`) are missing from the NDK's
  `GLES3/gl3ext.h`; `android-opengl-render.cpp` includes
  `<GLES2/gl2ext.h>` for them (valid alongside the ES3 headers). On a device
  without `EXT_texture_filter_anisotropic` the query errors and the value
  stays 1.0f, so the anisotropy block is skipped — graceful degradation.

### Verification (24 November 2026)

- OpenGL Android: `./cmake-build-android.sh opengl debug -clean` → exit 0,
  `libCPGE.so` 79,568,752 B (`android-opengl-build-8.log`; the clean build
  had exactly one error — `logLevelMessage` takes 2 arguments, not a format
  string — fixed by calling `logDebugMessage` instead, which is the
  formatting variant the shared code already uses).
- No device or emulator was available, so the EGL path is compile/link
  verified only — first frame on hardware is step 3 work.
