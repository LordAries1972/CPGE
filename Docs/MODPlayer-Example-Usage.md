# MODPlayer Class - Usage Guide

## Overview

`MODPlayer` plays ProTracker `.mod` modules through the same playback model
used by `XMMODPlayer`, `S3MPlayer`, `ITPlayer`, and `MPTMPlayer`: the module
is loaded and mixed to 44.1 kHz, 16-bit stereo PCM, then streamed through the
platform audio backend (DirectSound on Windows, miniaudio on Linux).

`MODPlayer` is the active tracker player in the default engine build. The
engine owns one global instance (`modPlayer`, selected by the
`__USE_MODPLAYER__` define in `Includes.h`); only one player define may be
active at a time.

## Basic Usage

```cpp
#include "MODPlayer.h"

void PlayMODExample() {
    MODPlayer player;

    if (player.Play(L"Assets/test1.mod")) {
        player.SetVolume(48);
        player.SetGlobalVolume(64);

        while (player.IsPlaying()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    player.Stop();
    player.Shutdown();
}
```

## Engine Selection

Enable the player by defining `__USE_MODPLAYER__` in `Includes.h` and
disabling the other music-player selection macros.

```cpp
#define __USE_MODPLAYER__
//#define __USE_XMPLAYER__
//#define __USE_S3MPLAYER__
//#define __USE_ITPLAYER__
//#define __USE_MPTMPLAYER__
```

## Supported Playback Features

- 44.1 kHz, 16-bit stereo output on Windows (DirectSound) and Linux
  (miniaudio ring, lock-free).
- ProTracker `.mod` patterns, samples, effects and loops.
- `Play`, `Stop`, `Pause` / `Resume`, `HardPause` / `HardResume`, `Mute`.
- `SetVolume`, `SetGlobalVolume`, `SetFadeIn`.
- `Shutdown()` stops playback and joins the playback thread. It is
  **idempotent**: it joins only if the thread is joinable and nulls the
  stream after deleting it, so calling it twice is safe.

## 24 November 2026 - Player Destructor (std::terminate Fix)

**What changed:** `MODPlayer` gained an explicit destructor in
`MODPlayer.h`:

```cpp
~MODPlayer() { Shutdown(); }
```

**Why:** `Play()` starts a playback thread (`PlaybackLoop`). If that thread
is still joinable when the global `modPlayer` is destroyed at process exit,
`~std::thread` calls `std::terminate()` and the engine dies with `SIGABRT`.
That is exactly what the Linux Xvfb crash diagnosis found (24 November
2026): the loader thread called `Play()` after the main thread had already
run `modPlayer.Shutdown()`, so the global was destroyed with a joinable
playback thread.

The destructor is a backstop, not the whole fix. The primary fix is the
Linux/Android shutdown order (`Docs/ThreadManager-Example-Usage.md`,
24 November 2026): the loader thread is joined *before*
`modPlayer.Shutdown()` runs, so `Play()` can no longer restart playback
after shutdown. The destructor covers any path that still reaches global
destruction with playback running, because `Shutdown()` is idempotent.

**How to use it:** nothing changes for callers. Keep calling `Shutdown()`
explicitly at the normal shutdown point; the destructor only fires if a
path skips it:

```cpp
// Normal shutdown (main.cpp) - unchanged:
modPlayer.Shutdown();

// If Shutdown() was missed, the destructor now does it instead of
// terminating the process:
// ~MODPlayer() { Shutdown(); }
```

**Known limitation:** the four sibling players (`ITPlayer`, `S3MPlayer`,
`MPTMPlayer`, `XMMODPlayer`) do **not** have this destructor yet. They are
never instantiated as the global in the current build (only one player
define is active), so the same latent hole exists only if the active player
define is ever switched. If it is, add the same one-line destructor to the
new active player.


## Linux / Android backend — miniaudio (23 November 2026)

On Linux and Android this player renders through miniaudio (vendored v0.11.26, `include/miniaudio.h`) instead of DirectSound: 44100 Hz stereo 16-bit PCM through a lock-free ring into a miniaudio device. No caller changes — the same load / play / shutdown calls work on every platform; the backend is selected by the platform guards at compile time. Covered by the Phase B sound test: 37 checks across all five tracker players, 0 failures (PCM observed at the callback; null backend — no audio device on the build machine, so nothing was actually heard).

## Platform-directive re-guard (24 November 2026)

The miniaudio backend in `Shutdown()` / `CreateAudioDevice()` / `FillAudioBuffer()` was in a bare `#else` (the unguarded default branch), which would not compile on macOS/iOS. Each is now explicitly `#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)`, with the Windows DirectSound backend under `#if defined(PLATFORM_WINDOWS)` and the disabled-stub fallback kept as `#else`. No behaviour change on any platform.
