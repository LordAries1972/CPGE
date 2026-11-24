# ScreenRecorder — Usage Guide

Real-time screen capture to MP4 (H.264 video + AAC audio) using the D3D11 back buffer
and WASAPI loopback. Part of the Cross Platform Game Engine (CPGE) by Daniel J. Hobson.

---

## Table of Contents

- [Overview](#overview)
- [Pipeline Summary](#pipeline-summary)
- [Timestamp Design (A/V Sync)](#timestamp-design-av-sync)
- [Encoding Specifications](#encoding-specifications)
- [Integration Checklist](#integration-checklist)
- [API Reference](#api-reference)
- [Step-by-Step Integration](#step-by-step-integration)
- [In-Engine Wiring (CPGE)](#in-engine-wiring-cpge)
- [On-Screen Recording Indicator](#on-screen-recording-indicator)
- [Keyboard Toggle (HOME key)](#keyboard-toggle-home-key)
- [Output File](#output-file)
- [Threading Model](#threading-model)
- [Error & Fallback Behaviour](#error--fallback-behaviour)
- [Common Mistakes](#common-mistakes)
- [Shutdown / Cleanup Order](#shutdown--cleanup-order)

---

## Overview

`ScreenRecorder` captures every rendered frame directly from the D3D11 swap chain back
buffer and simultaneously records system audio via WASAPI loopback. Both streams are
muxed into a single MP4 file through a Windows Media Foundation Sink Writer.

The class is designed to be called from within an existing D3D11 render loop with
minimal integration effort — three method calls is all that is required.

---

## Pipeline Summary

```
Render Thread                          Audio Thread (dedicated)
─────────────────────────────────      ─────────────────────────────────────
IDXGISwapChain1::GetBuffer(0)          WASAPI loopback IAudioCaptureClient
  └─ CopyResource → STAGING texture     └─ PCM / IEEE-float packets (10ms poll)
       └─ Map (CPU read)                      └─ silence fill if no data
            └─ Pack BGRA rows                      └─ IMFSinkWriter::WriteSample
                 └─ IMFSinkWriter::WriteSample           [MF: PCM → AAC encoder]
                      [MF: ARGB32 → NV12 → H.264]

Both streams → single IMFSinkWriter → .mp4 file
```

---

## Timestamp Design (A/V Sync)

A/V sync is achieved by tying both streams to one shared clock: the **video frame counter**.

| Stream | Timestamp source |
|--------|-----------------|
| Video  | `frameIndex × framePeriod` — strict counter, incremented once per captured frame |
| Audio  | Sample accumulator starting at 0. During silence (no WASAPI data), zeroed PCM is written to advance the audio timeline to match the current video frame counter exactly. |

Both start at 0 when `BeginWriting` succeeds and `m_pAudioClient->Start()` is called.
Because both reference `m_videoFrameIndex × framePeriod`, there is no QPC dependency,
no `Sleep()` imprecision, and no drift regardless of OS scheduler jitter.

**Critical:** silence must always be written when WASAPI returns no packets. If the audio
accumulator stalls during quiet periods while video advances, the gap is permanent and
audio will appear to lead on playback.

---

## Encoding Specifications

| Property         | Value                          |
|------------------|--------------------------------|
| Video codec      | H.264 (hardware-accelerated)   |
| Video bitrate    | 10 Mbps                        |
| Frame rate       | Configurable: 15 / 30 / 60 / 90 / 120 fps |
| Pixel format     | BGRA8 → NV12 (MF converts)     |
| Audio codec      | AAC                            |
| Audio bitrate    | 192 kbps                       |
| Audio source     | WASAPI loopback (system audio) |
| Container        | MPEG-4 (.mp4)                  |

---

## Integration Checklist

- [ ] COM initialised with `CoInitializeEx` before any recorder calls
- [ ] `MFStartup(MF_VERSION)` called after COM init
- [ ] `ScreenRecorder screenRecorder;` declared as a global
- [ ] `StartRecording` called with **client area** dimensions (not window size)
- [ ] `CaptureFrame` called once per frame **before** `IDXGISwapChain1::Present`
- [ ] `StopRecording` called **before** `renderer->Cleanup()` (D3D11 still alive)
- [ ] `MFShutdown()` called before `CoUninitialize()`

---

## API Reference

### `bool StartRecording(UINT width, UINT height, RecordFPS fps, const std::wstring& outputPath)`

Opens the MP4 file and initialises both the video and audio pipelines.

| Parameter    | Description                                                  |
|--------------|--------------------------------------------------------------|
| `width`      | Client area width in pixels (use `GetClientRect`, not `winMetrics`) |
| `height`     | Client area height in pixels                                 |
| `fps`        | Target frame rate — `RecordFPS::FPS_30`, `FPS_60`, or `FPS_120` |
| `outputPath` | Destination file path, e.g. `L"Assets\\recording.mp4"`      |

Returns `true` on success. Returns `false` and logs an error if the sink writer or
H.264 stream cannot be created. Audio failure is non-fatal — recording continues
video-only with a warning logged.

**Supported fps values:**

```cpp
enum class RecordFPS : UINT32
{
    FPS_15  = 15,
    FPS_30  = 30,
    FPS_60  = 60,
    FPS_90  = 90,
    FPS_120 = 120
};
```

---

### `void CaptureFrame(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain1* swapChain)`

Captures the current back buffer and writes it as a video sample.

- **Must be called once per frame, immediately before `Present`.**
- Safe to call unconditionally — instant no-op when not recording.
- Automatically recreates the internal staging texture if the back buffer dimensions
  change (window resize during recording is handled gracefully).

---

### `void StopRecording()`

Stops recording, finalises the MP4 (writes the `moov` atom footer), joins the audio
thread, and releases all D3D11 and Media Foundation resources.

- **Must be called before `renderer->Cleanup()`** so the staging texture
  `ComPtr` is dropped while the D3D11 device is still alive.
- Safe to call even if recording has already stopped (no-op).
- The destructor calls this automatically as a safety net.

---

### `bool IsRecording() const`

Returns `true` while a recording session is active.

---

### `RecordFPS GetTargetFPS() const`

Returns the fps value passed to the most recent `StartRecording` call.

---

## Step-by-Step Integration

### 1 — Prerequisites in `main.cpp`

```cpp
#include "ScreenRecorder.h"

// Global instance alongside other engine systems
ScreenRecorder screenRecorder;
std::atomic<bool> bRecordingToggleRequested{ false };
```

COM and Media Foundation must be started before any recording takes place:

```cpp
// Inside WinMain, after window creation
if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    return EXIT_FAILURE;

if (FAILED(MFStartup(MF_VERSION)))
    debug.logLevelMessage(LogLevel::LOG_WARNING,
        L"Media Foundation startup failed – screen recording unavailable.");
```

---

### 2 — Capture inside the Render Loop

Call `CaptureFrame` **before** `Present` in your render function:

```cpp
// DXRenderFrame.cpp — inside RenderFrame(), just before m_swapChain->Present()

screenRecorder.CaptureFrame(m_d3dDevice.Get(), m_d3dContext.Get(), m_swapChain.Get());

HRESULT hr = m_swapChain->Present(syncInterval, 0);
```

---

### 3 — Starting and Stopping

> **Important:** Never call `StartRecording` or `StopRecording` from inside a
> `WH_KEYBOARD_LL` hook callback. The hook runs on the message pump thread and
> `MFCreateSinkWriterFromURL` can block for up to 2 seconds initialising the
> H.264 encoder, which will freeze the entire application. Use the flag pattern
> shown below.

**Correct pattern — set a flag in the hook, act on it in the game loop:**

```cpp
// Inside the WH_KEYBOARD_LL / KeyUpHandler callback — returns instantly
case KeyCode::KEY_HOME:
    bRecordingToggleRequested.store(true);
    break;

// Inside the main game loop — safe to block here for encoder init
if (bRecordingToggleRequested.exchange(false))
{
    if (screenRecorder.IsRecording())
    {
        screenRecorder.StopRecording();
        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
    }
    else
    {
        RECT clientRect = {};
        GetClientRect(hwnd, &clientRect);
        UINT w = static_cast<UINT>(clientRect.right  - clientRect.left);
        UINT h = static_cast<UINT>(clientRect.bottom - clientRect.top);

        if (screenRecorder.StartRecording(w, h, RecordFPS::FPS_60, L"Assets\\recording.mp4"))
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
    }
}
```

---

### 4 — Shutdown Order

```cpp
// 1. Stop recorder while D3D11 device is still alive
screenRecorder.StopRecording();

// 2. Now safe to destroy the renderer
renderer->Cleanup();

// 3. Shut down Media Foundation before COM
MFShutdown();
CoUninitialize();
```

---

## In-Engine Wiring (CPGE)

| File                 | Role                                                                 |
|----------------------|----------------------------------------------------------------------|
| `main.cpp`           | Global instance, `MFStartup`/`MFShutdown`, toggle logic, shutdown   |
| `DXRenderFrame.cpp`  | `CaptureFrame` call before `Present`, blinking REC overlay           |
| `KBHandlersCode.cpp` | HOME key sets `bRecordingToggleRequested` flag (hook-safe)           |
| `ScreenRecorder.h`   | Class declaration                                                    |
| `ScreenRecorder.cpp` | Full implementation                                                  |

---

## On-Screen Recording Indicator

A blinking **● REC** indicator is rendered in the top-left corner of the screen
during active recording. It is drawn during the Direct2D pass in `DXRenderFrame.cpp`
after the GUI and before `EndDraw`, so it always appears on top of all game content.

- Colour: red (`MyColor::Red()`)
- Font size: 18pt
- Blink cycle: visible for 30 frames, hidden for 30 frames (~0.5 s at 60 fps)

```cpp
// DXRenderFrame.cpp — inside the D2D pass, before EndDraw
if (screenRecorder.IsRecording())
{
    static int recBlinkCounter = 0;
    recBlinkCounter = (recBlinkCounter + 1) % 60;
    if (recBlinkCounter < 30)
    {
        DrawMyText(L"\u25CF REC",
            Vector2(12.0f, 12.0f),
            MyColor::Red(),
            18.0f);
    }
}
```

---

## Keyboard Toggle (HOME key)

| Scene        | Behaviour                          |
|--------------|------------------------------------|
| Any scene    | Toggles recording on / off         |

- Press **HOME** once to begin recording to `Assets\recording.mp4`.
- Press **HOME** again to stop and finalise the file.
- A beep sound (`SFX_BEEP`) plays on both start and stop.
- Status is logged to the debug system at `LOG_INFO` level.

---

## Output File

Recordings are written to:

```
Assets\recording.mp4
```

Each new recording **overwrites** the previous file. Rename or move the file before
starting a new session if you need to keep both.

The file is not playable until `StopRecording()` is called, because the MP4 `moov`
atom (index / footer) is only written during `Finalize()`. If the application crashes
mid-recording, the file will be unplayable.

---

## Threading Model

| Thread         | Responsibility                                              |
|----------------|-------------------------------------------------------------|
| Render thread  | Calls `CaptureFrame` each frame; holds `m_mutex` during capture |
| Audio thread   | Polls WASAPI every 10 ms; fills silence from video frame counter; writes AAC samples |
| Main / UI      | Calls `StartRecording` / `StopRecording` (never from hook) |

`m_isRecording` is `std::atomic<bool>`. `m_videoFrameIndex` is `std::atomic<LONGLONG>` —
the audio thread reads it without a mutex to determine how much silence to insert each
poll cycle, keeping both streams on the same clock.

---

## Error & Fallback Behaviour

| Condition                          | Behaviour                                              |
|------------------------------------|--------------------------------------------------------|
| Audio device unavailable           | Falls back to video-only; logs `LOG_WARNING`           |
| `MFCreateSinkWriterFromURL` fails  | `StartRecording` returns `false`; logs `LOG_ERROR`     |
| Back buffer size changes           | Staging texture is recreated automatically             |
| `WriteSample` fails on a frame     | That frame is dropped; logs `LOG_WARNING`              |
| App crash mid-recording            | MP4 footer not written; file will be unplayable        |
| `StartRecording` while recording   | Returns `false` immediately (no double-start)          |
| No audio playing (silence)         | Zeroed PCM written to keep audio timeline in sync      |

---

## Common Mistakes

**Using `winMetrics.width/height` instead of `GetClientRect`**

`winMetrics` stores the full window size including the title bar and borders.
The swap chain back buffer only covers the client area. Passing the wrong size
causes `CopyResource` to fail every frame with a dimension mismatch error.

```cpp
// Wrong
screenRecorder.StartRecording(winMetrics.width, winMetrics.height, RecordFPS::FPS_60, path);

// Correct
RECT rc = {};
GetClientRect(hwnd, &rc);
screenRecorder.StartRecording(rc.right - rc.left, rc.bottom - rc.top, RecordFPS::FPS_60, path);
```

---

**Not writing silence during quiet periods**

If no silence is written when WASAPI returns zero packets, `m_audioTimestamp` stalls
while the video frame counter keeps advancing. When sound resumes it is stamped at the
old (stale) timestamp, placing it progressively earlier than the corresponding video —
audio appears to lead and the gap grows for the rest of the recording.

The implementation always writes zeroed PCM to fill the gap between `m_audioTimestamp`
and `videoFrameIndex × framePeriod` before draining real WASAPI packets.

---

**Calling `StartRecording` from a keyboard hook**

`WH_KEYBOARD_LL` fires on the message pump thread. `MFCreateSinkWriterFromURL`
can block for 500 ms to 2 seconds. Always use the flag pattern described in
[Step 3](#3--starting-and-stopping).

---

**Calling `StopRecording` after `renderer->Cleanup()`**

The staging texture is a `ComPtr<ID3D11Texture2D>`. If the D3D11 device is
destroyed first, releasing the texture triggers a debug layer warning and may
cause a crash. Always stop the recorder before tearing down the renderer.

---

**Not calling `MFShutdown` before `CoUninitialize`**

Media Foundation holds internal COM objects. Calling `CoUninitialize` first
leaves those objects in an invalid state. Always follow the order:
`StopRecording` → `MFShutdown` → `CoUninitialize`.


## Linux backend — FFmpeg (23 November 2026)

On Linux the recorder muxes H.264 (libx264) + AAC into MP4 with FFmpeg 8.1.2, instead of Media Foundation. Media test: 55 checks, 0 failures (shared with the movie player). Limits: the recorder captures tracker music but not SFX (the miniaudio engine has no output tap); microphone recording is not implemented on Linux — it warns once and records without a mic.

## Android backend — MediaCodec (24 November 2026)

On Android there is no FFmpeg (Daniel chose a native backend over vendoring
one — see the FFmpeg options analysis in
`scope/PHASE_D_STEP3_FFMPEG_ANALYSIS.md`), so the recorder encodes with the
NDK MediaCodec APIs (`mediandk`, minSdk 26) and muxes with `AMediaMuxer`.
The public API is unchanged — `StartRecording` / `StopRecording` /
`CaptureFrame` / `SetCaptureEnabled` work exactly as on Windows/Linux; only
the encode backend differs. The recorder captures tracker music but not SFX,
matching the Linux recorder — no scope was added.

### How it is wired

`ScreenRecorder.cpp` selects the backend the same way as the movie player:

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
per-platform files, `android-screenrecorder.h` /
`android-screenrecorder.cpp` (the renderer-restructure file style), plus the
platform-neutral converter `android-mediacodec-convert.h`. One structural
change: the shared tail of `StopRecording` (`CleanupVulkanCapture()` and the
audio-tap queue reset) was hoisted out of the FFmpeg arm so both backends
share it. Windows and Linux behaviour is unchanged — on Linux
`HAS_MEDIACODEC` is 0, so the FFmpeg path is selected exactly as before
(verified: Linux builds `PASSED: opengl vulkan`, 0 errors).

### What the backend does

* **Video encode:** `AMediaCodec` H.264 (`video/avc`), bitrate 10,000,000,
  frame rate from the `StartRecording` argument, I-frame interval 1 s.
  Colour format NV12 (21) is tried first with I420 (19) as the fallback;
  the encoder's output format (including `csd-0`) is read right after
  `start`, before the muxer starts. Each captured frame is converted with
  `BgraToYuv420` (`android-mediacodec-convert.h`) directly into the
  encoder's input buffer — encoder input buffers are tightly packed
  (stride == width), capacity-checked, and a frame is dropped if no input
  buffer is available (keeps the recording real-time).
* **Audio encode:** `AMediaCodec` AAC (`audio/mp4a-latm`), 48,000 Hz stereo,
  AAC profile 2 (LC). The miniaudio tap is installed the same way as the
  FFmpeg path; a streaming resampler (`AudioResampler`) converts the
  44,100 Hz stereo s16 tap output to 48,000 Hz s16, and whole 1024-sample
  AAC frames are emitted locked to the video clock. On flush the final
  partial frame is padded with silence, matching the FFmpeg path.
* **Muxing:** `AMediaMuxer_new(fd, MPEG_4)` on the recording file's fd
  (opened with `fopen`, falling back to `GetInternalDataPath()` when the
  given path is relative — the file is created under internal storage, and
  `mkdir -p` creates the parent directory). Video track is added first,
  then audio; the muxer starts only after both tracks are added. Encoder
  output is written with `AMediaMuxer_writeSampleData` using the original
  buffer offset/size. `StopRecording` removes the tap, flushes the audio
  frames, signals end-of-stream on both encoders and drains them
  (~2 s of output attempts), then stops and deletes the muxer.
* **How to use it** (same as Linux — no Android-specific call is needed):

```cpp
#include "ScreenRecorder.h"

// In main.cpp, after the renderer and sound are up:
ScreenRecorder recorder;
recorder.StartRecording(60, L"Assets/recording.mp4", true);  // fps, path, capture audio

// Inside the render loop (OpenGL or Vulkan):
recorder.CaptureFrame(width, height);  // or CaptureFrame(width, height, timestamp)

// On shutdown (or the HOME key toggle):
recorder.StopRecording();
```

On Android the relative path resolves under internal storage (the same
`GetInternalDataPath()` the engine uses for `GameConfig.cfg`), so the
recording lands where the app can read it back without storage permissions.

### Limits (24 November 2026)

* Compile/link verified only — both Android builds exit 0 with 0 errors
  (OpenGL `libCPGE.so` 80,476,328 B, Vulkan 82,201,008 B, `libmediandk.so`
  linked). **No device or emulator exists here, so no recording has ever
  been produced or played back** — first install + first frame on a real
  device is still outstanding.
* Microphone recording is not implemented on Android (same as Linux) — the
  recorder captures tracker music only.
* The encoder input buffers are assumed tightly packed (stride == width);
  if a device's encoder reports a different stride the frame will be
  mis-aligned (capacity-checked, so it cannot overrun).
