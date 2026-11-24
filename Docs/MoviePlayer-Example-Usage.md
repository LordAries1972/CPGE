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

## Android backend — MediaCodec (24 November 2026)

On Android there is no FFmpeg (Daniel chose a native backend over vendoring
one — see the FFmpeg options analysis in `scope/PHASE_D_STEP3_FFMPEG_ANALYSIS.md`),
so the movie player decodes with the NDK MediaCodec APIs (`mediandk`,
minSdk 26). The public API is unchanged — `OpenMovie` / `Play` / `Pause` /
`Stop` / `SeekTo` / `UpdateFrame` / `GetCurrentFrameRGBA` all work exactly as
on Windows/Linux; only the decode backend differs.

### How it is wired

`MoviePlayer.cpp` selects the backend with two defines, evaluated in the
non-Windows section:

```cpp
#if defined(PLATFORM_ANDROID)
    #define HAS_FFMPEG 0        // no FFmpeg on Android
    #define HAS_MEDIACODEC 1    // MediaCodec is always present (minSdk 26)
#else
    #define HAS_FFMPEG <FFmpeg found at build time>
    #define HAS_MEDIACODEC 0
#endif
```

Every FFmpeg-only call site is now guarded `#if !HAS_FFMPEG && !HAS_MEDIACODEC`
(the "no backend at all" case), with an `#elif HAS_MEDIACODEC` arm calling the
`…MediaCodecLocked` method. The Android backend itself lives in its own
per-platform files, `android-movieplayer.h` / `android-movieplayer.cpp`
(the renderer-restructure file style), plus the platform-neutral converter
`android-mediacodec-convert.h`. Windows and Linux behaviour is unchanged —
on Linux `HAS_MEDIACODEC` is 0, so the FFmpeg path is selected exactly as
before (verified: the Linux OpenGL run under Xvfb still shows
`[SCENE] Movie initialized successfully`).

### What the backend does

* **Demux** with `AMediaExtractor`: the video track (H.264) and the audio
  track (AAC) are found by mime type; width, height and duration come from
  the video track format (`AMEDIAFORMAT_KEY_DURATION`, microseconds — there
  is no `AMediaExtractor_getDuration` in the NDK).
* **Opening the movie file:** the path `OpenMovie` receives
  (`<internal>/Assets/cpge.mp4`) is not a regular file — the assets live
  inside the APK, deflated, so the open tries (1) the path as a regular file
  (works for a copy in internal storage), then (2) streaming the asset into
  a cache file under `<internal>/cpge-movie-cache/` and opening that (the
  copy is validated by size, so the 23 MB `cpge.mp4` is copied once, not on
  every open). There is no `AAssetManager_openFd` in the NDK C API
  (openFd is Java-only), so a cached copy is the only asset route.
* **Video decode:** `AMediaCodec` decodes H.264 into output buffers. The NDK
  has no `AMediaCodec_getOutputImage` / `AMediaImage` API, so frames come out
  as raw YUV420 buffers and are converted with `Yuv420ToBgra`
  (`android-mediacodec-convert.h`) into the BGRA format the existing overlay
  path expects. The output format's stride, slice height and colour format
  are read on `INFO_OUTPUT_FORMAT_CHANGED`; planar I420, semi-planar NV12
  and the flexible format (assumed NV12) are handled. A frame decoded ahead
  of the media time is held (its buffer index is kept, not released) and
  presented on the next call — the same hold-one-frame behaviour as the
  FFmpeg path.
* **Audio decode:** AAC is decoded with a second `AMediaCodec` into PCM and
  fed to the miniaudio ring (`Miniaudio::Stream`) exactly as the Linux
  FFmpeg path does. The output format's sample rate, channel count and PCM
  encoding are read on format change (the `slice-height` and `pcm-encoding`
  keys are used as literal strings — the symbols are API 28, so they are
  not in the minSdk 26 headers). A streaming resampler
  (`AudioResampler`, `android-mediacodec-convert.h`) converts to the
  44,100 Hz stereo s16 the ring expects. Audio input is gated on the ring
  level (target 8,192 samples): when the ring is full the sample is
  *dropped* (the extractor advances past it) so a video sample behind it
  is not blocked — the same backpressure tradeoff as the FFmpeg path.
* **Playback control:** `Play` / `Pause` / `Stop` / `Reset` / `SeekTo` /
  `UpdateFrame` mirror the FFmpeg versions, including the resume-anchor
  logic (wall clock vs media time) and the `AMediaExtractor_seekTo`
  `PREVIOUS_SYNC` + `AMediaCodec_flush` reset on seek/stop. One deliberate
  difference: MediaCodec keeps draining decoder output until both decoders
  report end-of-stream, so the tail of a movie plays out; the FFmpeg path
  stops at file exhaustion and drops the tail.

### Converter tests (host, 24 November 2026)

`android-mediacodec-convert.h` is plain C++17 with no Android includes, so
it is validated on the host against libswscale / libswresample
(`scope/test-mediacodec-convert.cpp`): **37 checks, 0 failures**.
YUV→BGRA 1:1 mean error 0.51–0.54 (max 3–4); BGRA→YUV 1:1 mean 0.08–0.20
(max 1); the resampler is bit-identical between one-shot and chunked
feeding. Downscale max errors are hard-edge filter-phase artefacts (a
smooth-pattern downscale scores mean 0.504, max 4).

### Limits (24 November 2026)

* Compile/link verified only — both Android builds exit 0 with 0 errors
  (OpenGL `libCPGE.so` 80,476,328 B, Vulkan 82,201,008 B, `libmediandk.so`
  linked). **No device or emulator exists here, so playback has never
  been seen or heard** — first frame on a real device is still outstanding.
* The output format's colour format is assumed NV12 for the flexible
  format; if a device reports a different flexible layout the frame will
  be wrong (it will still be a valid YUV420 frame, just mis-coloured).
* The `MaxOutputFrames` resampler contract: the caller must size its
  buffer to `ceil(inFrames / ratio) + 2`; if the buffer fills first, the
  tail of that chunk is dropped (a caller sizing bug, never a crash).
