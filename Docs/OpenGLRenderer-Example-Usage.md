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
