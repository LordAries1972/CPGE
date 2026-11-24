# AndroidApp — Shared NativeActivity State (Android)

Android-only helper (Phase D step 2, 24 November 2026). Holds the single
`android_app` pointer set once by `android_main()` and hands it out to the
subsystems that need it, so none of them keeps its own copy of the activity
state. On every other platform the header declares nothing.

## Files

- `AndroidApp.h` / `AndroidApp.cpp` — the accessor (Android-only, guarded by
  `PLATFORM_ANDROID`).
- `main.cpp` — `android_main()`, the NativeActivity entry point, plus the
  app-command and input-event glue callbacks.
- `WinSystemAndroid.cpp` — `SystemUtils` for Android (internal-storage path,
  window size, display density); reads the activity through `AndroidApp`.
- `FileIO.cpp` — APK asset reads through `AAssetManager` (see
  `FileIO-Example-Usage.md`).
- `Debug.cpp` — log file in internal storage + logcat mirror (see
  `Debug-Example-Usage.md`).
- `Android/AndroidManifest.xml` + `Android/app/build.gradle` — the app that
  loads `libCPGE.so` (`android.app.lib_name` = `CPGE`).

## API

```cpp
#include "AndroidApp.h"

void SetApp(struct android_app* app);   // called once by android_main()
struct android_app* GetApp();           // the pointer, or nullptr before SetApp
AAssetManager* GetAssetManager();       // APK assets, or nullptr before SetApp
std::string GetInternalDataPath();      // activity->internalDataPath, or "" before SetApp
ANativeWindow* GetNativeWindow();       // current window, or nullptr before INIT_WINDOW
```

The pointer is owned by the glue/activity, never by `AndroidApp`. Every
accessor is safe before `SetApp` (returns `nullptr` / empty string); callers
must handle that — `Debug` resolves the log path per write for exactly this
reason (the `Debug` global is constructed before `android_main()` runs).

> Gotcha (Vulkan Android build 6): `AndroidApp.h` originally included
> `Includes.h` *inside* its `#if defined(PLATFORM_ANDROID)` guard, so the
> guard was false in any TU that had not already included `Includes.h` —
> `AndroidApp.cpp` compiled to an empty file and the link failed on all five
> accessors. `Includes.h` now comes first, before the guard. If you add a
> new header with a `PLATFORM_*` guard, include the header that defines the
> macro *before* the guard, never inside it.

## android_main() flow (main.cpp)

1. `AndroidApp::SetApp(app)`; register `onAppCmd` (`HandleAndroidAppCmd`)
   and `onInputEvent` (`HandleAndroidInputEvent`).
2. Block on the looper (`ALooper_pollOnce(-1, ...)` — `ALooper_pollAll` is
   marked unavailable by the NDK) until `app->window` exists.
3. `baseDir = sysUtils.Get_Current_Directory()` (internal storage) and
   `config.setConfigFile(internalPath + "/GameConfig.cfg")`; a missing file
   loads defaults and saves them.
4. `CreateRendererInstance()`, `hwnd = ANativeWindow*`, `RunEngine(nullptr, 0)`.
5. `s_androidEngineStarted` guards against a second `INIT_WINDOW`.

App commands: `INIT_WINDOW` after engine start and `TERM_WINDOW` /
`DESTROY` set the quit flag; `PAUSE` / `RESUME` / `START` / `STOP` / focus /
resize are logged with no engine hook yet (documented limitation).

## Input dispatch (main.cpp)

`HandleAndroidInputEvent` routes by event type and source:

- Key events → `KeyboardHandler::HandleAndroidInputEvent` first; if it does
  not consume the event, `Joystick::HandleAndroidInputEvent` (gamepad
  buttons fall through to the joystick).
- Motion events with a joystick/gamepad source → `Joystick`.
- Touchscreen motion → pointer/mouse for the GUI (pointer 0 only), mirroring
  `HandleLinuxEvent`.

Input callbacks run on the engine thread (the glue calls `onInputEvent` from
the thread that polls the looper), so no extra threading is needed.

## How to use (example)

```cpp
// Somewhere that needs a writable file on Android:
#include "AndroidApp.h"

std::string SavePath(const std::string& name)
{
    const std::string dir = AndroidApp::GetInternalDataPath();
    if (dir.empty())
        return name;            // not on Android, or before android_main()
    return dir + "/" + name;
}
```

```cpp
// Reading a read-only game asset on Android (see FileIO for the
// task-based version — this is the raw form):
AAssetManager* mgr = AndroidApp::GetAssetManager();
AAsset* asset = mgr ? AAssetManager_open(mgr, "textures/logo.dds",
                                          AASSET_MODE_BUFFER) : nullptr;
```

## Verification (24 November 2026)

Vulkan Android `libCPGE.so` links clean with `android_main` + all five
accessors (`android-vulkan-build-8.log`, exit 0). No device or emulator was
available, so the activity lifecycle is untested on hardware — that is step
3 work (see `scope/PHASE_D_STEP2_VERIFICATION.md`).
