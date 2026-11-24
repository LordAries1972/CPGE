# Debug Class - Usage Guide

## Overview

`Debug` is the engine logging system. Every subsystem logs through it to
`DebugLog.txt` (and to the debugger / console where available). It provides
log levels, a diagnostic log path, and the shared fatal-message path
`EmitMessage` that every `logLevelMessage` / `logDiagLevelMessage` call
funnels through.

The two quit flags live in `Platform.h` and are visible on every platform:

```cpp
inline std::atomic<bool> g_cpgeQuitRequested{ false }; // Engine quit requested
inline std::atomic<bool> g_cpgeMainLoopActive{ false }; // Main loop is running
```

## Basic Usage

```cpp
#include "Debug.h"

// Normal log message at a given level:
debug.logLevelMessage(LogLevel::LOG_INFO, L"Renderer initialised");

// Diagnostic message (only shown at the configured diagnostic level):
debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Joystick not present, keyboard fallback");

// Shared output path (used by both of the above):
Debug::EmitMessage(LogLevel::LOG_ERROR, L"Failed to open texture file", true);
```

## Log Levels

```cpp
enum class LogLevel {
    LOG_INFO,
    LOG_DEBUG,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL,
    LOG_TERMINATION,
    // ...
};
```

- `LOG_INFO` / `LOG_DEBUG` - routine progress.
- `LOG_WARNING` - something failed but the engine continues (for example a
  joystick hardware fault - see `Docs/Joystick-Example-Usage.md`).
- `LOG_ERROR` / `LOG_CRITICAL` - a serious failure. On Windows these show a
  message box and post a quit message (unchanged). On Linux/Android their
  quit behaviour is gated - see 24 November 2026 below.
- `LOG_TERMINATION` - always fatal on every platform; the engine exits
  immediately.

## 24 November 2026 - Quit-on-CRITICAL Hardening (Linux/Android)

**What changed:** `EmitMessage` (`Debug.cpp`) used to set
`g_cpgeQuitRequested` for *any* `LOG_ERROR` / `LOG_CRITICAL` on non-Windows.
Every logged error became an immediate quit, even during startup before the
main loop existed. That is what killed the engine ~7 seconds after launch:
the intro movie path never resolved on Linux, `OpenMovie` failed at
`LOG_CRITICAL`, and the engine tore itself down without ever running its
main loop.

The non-Windows branch is now gated on the main loop actually running:

```cpp
// Debug.cpp - EmitMessage, non-Windows branch:
if (level == LogLevel::LOG_TERMINATION) {
    // LOG_TERMINATION is always fatal: exit immediately.
} else if (level == LogLevel::LOG_ERROR || level == LogLevel::LOG_CRITICAL) {
#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    // Only quit if the main loop is running. Init-time errors must not
    // quit the engine - their call sites return EXIT_FAILURE explicitly.
    if (g_cpgeMainLoopActive.load()) g_cpgeQuitRequested.store(true);
#else
    g_cpgeQuitRequested.store(true); // Other non-Windows: old behaviour.
#endif
}
```

`g_cpgeMainLoopActive` is set in `main.cpp` just before the `while` loop
(after `OpenStartMovieAndPlay()`) and cleared right after the loop exits.
The Windows branch is unchanged (message boxes + `PostQuitMessage`).

**Two related fixes in the same change (`Platform.h`):**

1. `g_cpgeQuitRequested` was declared *inside* an
   `#if !defined(PLATFORM_WINDOWS)` block, but `main.cpp` uses it in
   Windows-compiled code (the `PumpPlatformMessages` `WM_QUIT` path and the
   shared main loop), so the Windows build could not compile. The
   declaration was moved out of the guard so it is visible on all platforms.
   That also fixes a second Windows compile break in the pushed Linux port
   (the first is the `nCmdShow` defect in `main.cpp`, which is a separate
   follow-up and was left alone).
2. `#include <atomic>` was added unconditionally at the top of `Platform.h`.
   The non-Windows block already included it, but that include is skipped
   on Windows, and the flags are `std::atomic` on every platform.

**How to use it:** log errors as before. If an error happens during
initialisation, return `EXIT_FAILURE` from the init path (as the existing
init code already does) instead of relying on the quit flag. If an error
happens while the game is running, the flag is set and the main loop exits
cleanly at the top of its next iteration:

```cpp
// Init-time error - return, do not rely on the quit flag:
if (!renderer->Initialize(...)) return EXIT_FAILURE;

// Run-time error - the flag quits the loop cleanly:
debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Renderer device lost");
```

**Verification (24 November 2026, Linux OpenGL under Xvfb):** with the
movie path fixed the engine no longer hits the critical at all; with the
movie deliberately broken (FFmpeg headers missing) the `LOG_CRITICAL` is
still logged and the engine continues into the intro and the main loop
instead of tearing down.
