# MoviePlayer Class - Usage Guide

## Overview

`MoviePlayer` plays video files (`.mp4`) as the engine intro movie and in-game
cutscenes. On Windows it uses the Windows media pipeline; on Linux it decodes
with FFmpeg (`libavformat` / `libavcodec` / `libswscale` / `libswresample`) into
BGRA frames that are handed to the renderer overlay path. The engine opens the
intro movie at startup from `main.cpp` (`OpenStartMovieAndPlay`, `cpge.mp4`)
and the test movie from `OpenMovieAndPlay` (`test1.mp4`).

The intro movie (`Assets/cpge.mp4`) is the credit sequence for the FOSS engine
and must stay in the build.

## Basic Usage

```cpp
#include "MoviePlayer.h"

// The engine owns the global MoviePlayer (moviePlayer). Open and play:
if (moviePlayer.OpenMovie(L"Assets/cpge.mp4")) {
    moviePlayer.Play();
}

// Per-frame: decode the next frame and draw it through the renderer overlay.
moviePlayer.UpdateFrame();
moviePlayer.Render(position, size);

// Stop when the scene changes or the movie ends.
moviePlayer.Stop();
```

## Engine Selection

Movie playback on Linux needs the FFmpeg 8.1.2 headers and runtime libraries.
The Linux CMake file detects them (`find_path` / `find_library`) and defines
`HAS_FFMPEG` when they are found; without them the player compiles out and
`OpenMovie` always fails with a warning (`MoviePlayer is unavailable on this
build`). The runtime libraries (`libavformat62`, `libavcodec62`,
`libavutil60`, `libswscale9`, `libswresample6`) must match the header version.

```cpp
// MoviePlayer.h - FFmpeg backend is selected by header detection:
#if defined(HAS_FFMPEG)
    // libavformat / libavcodec decode path (Linux)
#else
    // Windows media pipeline / disabled stub
#endif
```

## Supported Playback Features

- MP4 (H.264 video + AAC audio) decode to BGRA frames via FFmpeg on Linux.
- Audio routed through the miniaudio ring on Linux (same path as the tracker
  players), DirectSound / Media Foundation on Windows.
- `RequestMovie` / `ProcessMovieRequest` for queued playback with an optional
  held opening frame.
- `SeekTo`, `Pause`, `ResumeRequestedMovie`, `Reset` for cutscene control.
- `Render(position, size)` draws the current frame through the renderer
  overlay. **Known limit (Linux): the movie display path is wired for the
  Vulkan renderer only; on the OpenGL renderer the movie decodes and its
  audio plays, but the video frame is not displayed.** That display gap is
  Phase C work, not part of the 24 November 2026 path fix below.

## 24 November 2026 - Linux/Android Movie Paths

**What changed:** `main.cpp` (`OpenMovieAndPlay`, `OpenStartMovieAndPlay`)
used to build the movie path with Windows backslashes on every platform:

```cpp
// Before (all platforms - never resolves on Linux/Android):
std::wstring moviePath = baseDir + L"\\Assets\\cpge.mp4";
```

On Linux and Android the file was never found, `OpenMovie` failed at
`LOG_CRITICAL`, and the engine quit during startup (see `Docs/Debug-Example-Usage.md`,
24 November 2026, for why that critical no longer quits the engine before the
main loop starts). The path is now built per platform, each branch behind its
own condition directive:

```cpp
#if defined(PLATFORM_WINDOWS)
    std::wstring moviePath = baseDir + L"\\Assets\\cpge.mp4";
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    std::wstring moviePath = baseDir + L"/Assets/cpge.mp4";
#endif
```

The same split applies to `test1.mp4` in `OpenMovieAndPlay`. The Windows
branch is byte-identical to before; only the Linux/Android branch is new.

**How to use it:** place the movie in `Assets/` next to the binary (or run
from the repository root so `Assets/` resolves). No code change is needed to
play a different movie - call `OpenMovie` with the asset path:

```cpp
// Play a cutscene movie from anywhere in the game code:
moviePlayer.RequestMovie(L"Assets/cutscene01.mp4");
```

**Verification (24 November 2026, Linux OpenGL under Xvfb):** the log shows
`[SCENE] Entered SCENE_INTRO_MOVIE` and `[SCENE] Movie initialized
successfully`, with no `Failed to open Video File for Playback!` error. The
`LOG_CRITICAL` in the `OpenMovie` failure path was deliberately **not**
lowered - a missing movie is still a critical error; it just no longer
forces a quit before the main loop has started.


## Linux backend — FFmpeg (23 November 2026)

On Linux the movie player decodes with FFmpeg 8.1.2 (headers taken from the upstream tarball — apt only ships 6.1.1 dev files) instead of Media Foundation. Frames decode to BGRA and are uploaded into the Vulkan overlay path. Media test: 55 checks, 0 failures (shared with the screen recorder). Bugs fixed while porting: a pending-frame stall that froze the video, and a clock double-count that played movies at ~2x speed. The intro movie now initialises under Xvfb (`[SCENE] Movie initialized successfully`).

Limit: movie *display* on Linux is Vulkan-only — the OpenGL display path is still Windows-guarded. Decoding itself works on both renderers.
