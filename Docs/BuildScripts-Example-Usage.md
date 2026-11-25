# Build Scripts — cmake-build.sh, cmake-build-android.sh, cmake-build.bat, reset.sh / reset.bat

How CPGE is built on each platform, and the reset scripts that return the
game to a fresh start on clean release builds.

## Files

- `Linux/cmake-build.sh` — Linux desktop build (OpenGL / Vulkan).
  `./cmake-build.sh <renderer> [debug|release] [-clean] [-warnings]`
- `Linux/cmake-build-android.sh` — Android cross-compile (OpenGL ES /
  Vulkan) with the NDK, plus aapt2 APK packaging and the optional
  device install. `./cmake-build-android.sh <renderer> [debug|release]
  [abi] [-clean] [-warnings] [-install] [-no-install]`
- `cmake-build.bat` — Windows build (DX11 / DX12 / OpenGL / Vulkan).
  `cmake-build <renderer> [Debug|Release] [-clean]`
- `Linux/reset.sh` — reset the high scores and their recordings
  (POSIX shell; also runs under Git Bash on Windows).
  `./reset.sh [assets-dir]`
- `reset.bat` — the same reset for Windows. `reset.bat [assets-dir]`

Run each script's `help` directive for the full option list. The
dependency check (both scripts verify every dependency and download
what is missing before building, debug and release alike) and the
Android dex auto-build + device install are documented in
`Android/README.md` and `ReleaseInfo.md` (25 November 2026).

## What a build does

Linux (`cmake-build.sh`): checks dependencies, configures and builds
into `Linux/build/<Renderer>/<Config>`, and on success installs the
executable and its shaders into the project root, so the executable
runs from the root where `Assets/` lives. `all` builds OpenGL then
Vulkan and installs once at the end, only if every pipeline passed.

Android (`cmake-build-android.sh`): checks dependencies, builds
`libCPGE.so` with the NDK toolchain into
`Linux/build-android/<abi>/<Renderer>/<Config>`, and on success
installs the `.so` to the project root and to the Gradle project's
`jniLibs/<abi>/`, syncs `Assets/` to the Gradle project's `assets/`
(`AIHistory` excluded), verifies the NativeActivity manifest entry,
builds `classes.dex`, packages the APK with aapt2 (arm64-v8a only),
and offers to install it on a connected device. Release builds are
stripped with `llvm-strip` and are not debuggable.

Windows (`cmake-build.bat`): increments the build number, patches
`Includes.h` for the chosen renderer, configures and builds into
`build\<Renderer>\<Config>` (Visual Studio 17 2022, x64), and on
success runs `install-debug.bat` / `install-release.bat`. `all` builds
every pipeline in turn. `clean` deletes the build trees and exits.

## Reset — high scores and recordings (25 November 2026)

On a **clean release build only** — both `-clean` AND `release` — the
build scripts reset the game to a fresh start (Daniel, 25 November
2026): the high-score table and its recordings are deleted, so a clean
release build never ships with stale scores. Debug builds, builds
without `-clean`, and the bare `clean` directive (which only deletes
build trees) never reset.

What the reset deletes, both inside the `Assets` tree:

1. `<assets>/highscores.txt` — the `MyHighScores` table
   (`Docs/Vectors-Example-Usage.md`: the table is saved as
   `S <game> <score> <wave> <name> ...` / `W <game> <bestWave>` lines
   with a `CPGE_HIGHSCORES_2` header; the engine binds it with
   `scores.SetFile(assetsDir / "highscores.txt")`).
2. The recording files in `<assets>/Playbacks/` — the videos the table
   references: `<id>.mp4`, the `<id>.json` manifest sidecar, and the
   dated object-state files `<scene>-<YYYY-MM-DD_HH-MM-SS>-<id>.cpr`
   (`Vectors.cpp`: `MyHighScores::RecordingPath` /
   `DiscardRecording` / `PruneRecordings`). Only those three patterns
   are removed; anything else in `Playbacks/` is left alone, exactly
   as the engine's own `PruneRecordings()` never touches unrelated
   files. Once the table file is gone every `Playbacks/` file is
   unreferenced, so deleting them is consistent with the engine's own
   semantics.

The `Playbacks/` **folder itself is kept**, and the recordings are
deleted by contents only: nothing in the engine creates `Playbacks/`
on demand (no `create_directories` call for it anywhere; the FFmpeg
`avio_open` and the MediaCodec `fopen` writers both fail when the
parent directory does not exist), so removing the folder would break
the next recording.

Manual screen captures (`Assets/recording_YYYYMMDD_HHMMSS.mp4`,
written by `main.cpp`) are NOT table recordings and are NOT deleted.

Both scripts are idempotent (exit 0 when there is nothing to remove),
print what they removed, and anchor every path to the script's own
location — never to the caller's working directory. A different assets
dir may be passed as the first argument; it must be a folder named
`Assets`, or the script refuses to touch it. Nothing outside an
`Assets` tree is ever deleted.

### Where the reset runs

- `Linux/cmake-build.sh` — after a successful build and install, when
  `CONFIG` is `Release` and `-clean` was given. For `all`, each
  pipeline resets after its own build (idempotent, so later runs find
  nothing to do).
- `Linux/cmake-build-android.sh` — at the start of
  `install_and_package_android()`, before the `Assets/` sync, when
  `CONFIG` is `Release` and `-clean` was given — so the staged
  `Assets/` (and the APK packaged from it) carry a fresh table. The
  build-machine reset is a hard failure: a failed reset stops the
  build before packaging, the same way a failed dex build or a
  missing `llvm-strip` does.
- `cmake-build.bat` — after a successful build, before the install
  script, when `CONFIG` is `Release` and `-clean` was given. A failed
  or missing `reset.bat` stops the build. For `all`, each pipeline
  resets after its own build.

`reset.sh` is invoked through `bash` on purpose: the GitHub push tool
does not preserve file modes, so a pulled `reset.sh` may be mode 644 —
`bash` runs it either way.

### Android: on-device reset

A fresh install starts clean, but `adb install -r` preserves internal
storage, so a clean release build also clears the on-device table when
a device is connected (`reset_android_device()` in
`Linux/cmake-build-android.sh`, every connected device in state
`device`, best effort — it never fails the build).

Paths: the engine's writable home on Android is internal storage
(`AndroidApp::GetInternalDataPath()`, `/data/data/<pkg>/files`) and
its runtime `Assets/` is `baseDir + "/Assets"` (`main.cpp`), so the
table is `<files>/Assets/highscores.txt` with recordings in
`<files>/Assets/Playbacks/`. The `<files>/` root copies are cleared
too — the `SetFile` binding has no caller in the current code, so both
locations are plausible and both live inside the app's own storage.
The package defaults to `com.ultimanium.cpge`
(`CPGE_ANDROID_PACKAGE` overrides it).

Access: `/data/data/<pkg>` is not readable by the `adb shell` user,
so the reset uses `run-as <pkg>` — which only works for debuggable
apps (debug builds). A release build is not debuggable, so on a
release install the device step prints a note and the build still
succeeds; a debug build on a connected device is cleared properly.
No device, or no `adb`, prints a one-line note and skips. If the game
is running on the device, close it first — it would rewrite the table
when it exits.

### Known gaps (25 November 2026)

- The high-score binding has no caller in the current code:
  `MyHighScores::SetFile()` is defined in `Vectors.cpp` but nothing
  calls it, so the engine does not write `highscores.txt` yet — the
  reset targets the location the docs and the class define, and will
  take effect as soon as the binding is wired up.
- Nothing creates `Playbacks/` on demand (see above). On a fresh
  checkout — or after a reset that emptied it — the folder exists in
  the repo tree, but a runtime that has never recorded has no
  `Playbacks/` directory at all, and the first recording would fail
  to open its output file. That is a pre-existing engine gap, not
  something the reset scripts fix: they delete, they never create.
- `reset.bat` is Windows-unverified: there is no Windows machine in
  this VM, so it was reviewed against `cmake-build.bat`'s conventions
  only. Daniel's Windows run proves it.

## Verification (25 November 2026)

- Scratch `Assets/` tree: a fake `highscores.txt`
  (`CPGE_HIGHSCORES_2` header, `S`/`W` lines), `Playbacks/` holding
  `<id>.mp4`, `<id>.json`, a `<scene>-<date>-<id>.cpr`, and an unrelated
  `keep-me.txt` inside `Playbacks/` plus an unrelated `notes.txt` in
  `Assets/` — `reset.sh` removed exactly the table and the three
  recording files, kept the `Playbacks/` folder and both unrelated
  files; a second run exited 0 with "nothing to remove"; a run with
  no files present exited 0. A non-`Assets` target was refused.
- Wiring: a real `./cmake-build.sh opengl release -clean` build
  end-to-end showed the reset running in the log and the planted
  scores gone afterwards; an incremental `./cmake-build.sh opengl
  release` (no `-clean`) left planted scores untouched and printed
  no reset line. The gate (`Release` AND `-clean`) was verified by
  inspection in all three drivers, including the bare `clean`
  directive in each (exits before any build, never resets).
- The Android on-device reset was verified with a fake `adb` shim
  (one hardware device + one emulator): the exact `run-as` removal
  commands ran for both devices; no devices and a missing `adb` both
  printed a one-line note and exited 0. A real-device reset is
  unverified — no device in this VM.
- `bash -n` clean on `reset.sh`, `cmake-build.sh` and
  `cmake-build-android.sh`. Full record: `scope/RESET_VERIFICATION.md`.
