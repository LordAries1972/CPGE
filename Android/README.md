# CPGE Android app (Phase D)

NativeActivity app that packages the engine (`libCPGE.so`) as an APK. Created
24 Nov 2026 (Phase D step 2).

## Layout

- `app/src/main/AndroidManifest.xml` — NativeActivity entry, `android.app.lib_name`
  = `CPGE` (must match `Linux/CMakeLists.txt` `OUTPUT_NAME "CPGE"`), `INTERNET`
  permission (NetworkManager BSD sockets), landscape, `configChanges` so the
  activity is not recreated on rotation.
- `app/build.gradle` — `namespace`/`applicationId` `com.ultimanium.cpge`
  (provisional — Daniel can change it; keep it consistent in the manifest and
  both gradle files), `compileSdk 34`, `minSdk 26`, `targetSdk 34`,
  `ndkVersion 27.2.12479018`, `externalNativeBuild` driving
  `../../Linux/CMakeLists.txt` with `-DRENDERER=vulkan`.
- `build.gradle` / `settings.gradle` — root project, Android Gradle Plugin 8.5.2.
- `gradle.properties` — pins the Temurin JDK 17 (see below) and the NDK version.
- `gradle/wrapper/` — Gradle 8.7 wrapper.
- `package-apk.sh` — manual aapt2 APK packaging (25 Nov 2026: moved into
  the repo from the workspace `scope/apk/` folder; the primary APK path).
- `build-dex.sh` — compiles the Java helpers to `classes.dex` (same
  folder; `package-apk.sh` adds it to the APK automatically).
- `app/src/main/java/com/ultimanium/cpge/CpgeTts.java` — the Java helper
  for Android TTS (Phase D step 3): a thin wrapper over
  `android.speech.tts.TextToSpeech`, driven through JNI from the native
  `TTSManager`. The manifest no longer declares `hasCode="false"`, so
  the APK carries `classes.dex` for it (built by
  `build-dex.sh` in this folder; Gradle picks the sources up from the
  standard `src/main/java` location automatically).

## Prerequisites

- Android SDK under `~/workspace/android-sdk` (NDK r27c 27.2.12479018,
  platform android-34, build-tools 34.0.0). `ANDROID_HOME` /
  `ANDROID_SDK_ROOT` must point at it when running Gradle.
- Full Temurin JDK 17 at `~/workspace/android-sdk/jdk-17.0.21.1+2` (installed
  24 Nov 2026 from Adoptium; the SDK tree previously had only a JRE, which
  cannot run Gradle). Pinned by `org.gradle.java.home` in `gradle.properties`,
  so `JAVA_HOME` does not need to point at it for Gradle.
- GLM headers — none needed. GLM 0.9.9.8 is vendored in the repo at
  `include/glm/` (Daniel manages updates by replacing that folder), and
  both the Android and Linux builds use it automatically on every host.
  No system install, no `GLM_ROOT`. `GLM_ROOT` remains only as an
  override (a different GLM copy to use instead of the vendored one).
  For the Android build the headers are staged into the build
  directory (`android-deps/`) — the host include path is never added
  to the NDK compile.
- Gradle 8.7 (wrapper; downloaded on first `./gradlew` run from
  `services.gradle.org`).
- `sdkmanager` does NOT work on this machine (Java proxy tunnelling fails);
  SDK packages were installed with `curl` from `dl.google.com` — use curl,
  not sdkmanager, for any new SDK package.
- `zip`, 7-Zip, 7za or WinZip for APK staging (25 November 2026):
  `package-apk.sh` stages the APK contents with `zip` when it is on
  PATH, else `7z`, else `7za` (the standalone 7-Zip console — NCH
  ExpressZip bundles 7za.exe, so a machine with ExpressZip on PATH has
  `7za` even though 7-Zip itself was never installed), else `wzzip`
  (WinZip's command-line tool, from the free WinZip Command Line Support
  Add-On — a separate download from winzip.com, NOT in base WinZip).
  7-Zip does NOT provide a `zip` command — its command is `7z` — so
  installing 7-Zip never satisfies a `zip` check. On Git Bash:
  `pacman -S zip` for `zip`, or install 7-Zip and put
  `C:\Program Files\7-Zip` on PATH for `7z`. A PATH change only applies
  to a NEW Git Bash window — close and reopen Git Bash after editing
  PATH. The script also looks up the 7-Zip install in the Windows
  registry (`HKLM\SOFTWARE\7-Zip`), the WinZip App Paths key
  (`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\wzzip.exe`),
  and checks the standard install locations on C: and D:, so a default
  install usually just works; if yours is renamed or moved, point the
  script straight at it with `CPGE_7Z='/d/Tools/7-Zip/7z.exe'` (or a
  7za.exe path).

## Build

```sh
export ANDROID_HOME="$HOME/workspace/android-sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
cd Android
./gradlew assembleDebug
# APK: app/build/outputs/apk/debug/app-debug.apk
```

The native build is the same `Linux/CMakeLists.txt` that
`Linux/cmake-build-android.sh` drives standalone; Gradle passes its own
`ANDROID_ABI` / `ANDROID_PLATFORM` / toolchain file, and `-DRENDERER=vulkan`
comes from `app/build.gradle`. To build the `.so` on its own first (and check
it links before packaging):

```sh
cd ../Linux && ./cmake-build-android.sh vulkan debug
```

`Linux/cmake-build-android.sh` also takes the options `-clean` and
`-warnings` (any position; run `./cmake-build-android.sh help` for the
full usage). Warnings are suppressed by default (quiet build); pass
`-warnings` to see them.

On a successful build the script automates the whole post-build flow
(25 November 2026), in order:
1. `libCPGE.so` is installed to the project root and to
   `app/src/main/jniLibs/<abi>/`.
2. `Assets/` is synced to `app/src/main/assets/` (`AIHistory` excluded).
3. The `AndroidManifest.xml` NativeActivity entry is verified (and
   repaired if missing or wrong). In plain words: NativeActivity is
   Android's built-in activity that starts the native `.so` directly,
   with no Java activity code of our own — the manifest names
   `android.app.NativeActivity` and the `android.app.lib_name`
   meta-data tells it which library to load (`CPGE`, i.e.
   `libCPGE.so`); the MAIN/LAUNCHER intent-filter puts the app in the
   launcher. GameActivity is the newer Jetpack alternative; this
   project uses NativeActivity.
4. The APK is built with aapt2 (`Android/package-apk.sh`), AFTER the
   install, so the APK packages the `.so` and `Assets/` — debug AND
   release, both as `cpge-<renderer>-<config>.apk` (arm64-v8a only;
   skipped with a clear note for another ABI). `package-apk.sh`
   takes the `.so` from the build dir for the requested config,
   falling back to the project-root copy. The packaging scripts find
   the SDK from `ANDROID_HOME` (or `ANDROID_SDK_ROOT`), build-tools
   from `ANDROID_BUILD_TOOLS_VERSION` (default 34.0.0, newest
   installed as fallback), and the JDK from `JAVA_HOME` (falling back
   to the SDK tree's JDK 17, then `java` on PATH); when a tool is
   genuinely missing they fail with what to install. The APK contents
   are staged with `zip`, `7z`, `7za` or `wzzip` (see Prerequisites —
   7-Zip provides `7z`, not `zip`; ExpressZip provides `7za`; WinZip's
   `wzzip` needs the Command Line Support Add-On). On Windows/Git
   Bash, `ANDROID_HOME` and `JAVA_HOME` may hold Windows-style paths
   (`D:\...`); the scripts normalise them to POSIX paths with
   `cygpath` and resolve tools with their Windows extensions
   (`apksigner.bat`, `d8.bat`, `aapt2.exe`, `zipalign.exe`) — 25
   November 2026.

Release APKs (25 November 2026): build with
`./cmake-build-android.sh vulkan release` (or `opengl release`) —
output is `Android/cpge-<renderer>-release.apk`. Release builds are
stripped of debug info with `llvm-strip` (from the NDK, resolved the
same way as the build script resolves it); a release build with no
`llvm-strip` fails loudly rather than shipping an unstripped `.so`.
The release APK is not debuggable (`aapt2 link` runs without
`--debug-mode`; the manifest has no `android:debuggable` attribute),
and debug builds stay debuggable and unstripped. Signing: a release
APK uses the release keystore when `CPGE_RELEASE_KEYSTORE` is set
(with `CPGE_RELEASE_KEYSTORE_PASS`, `CPGE_RELEASE_KEY_ALIAS` and
`CPGE_RELEASE_KEY_PASS` — all four required); otherwise it is signed
with the debug keystore and the script prints a NOTE saying so — set
a release keystore before distributing a release APK. `package-apk.sh`
also takes the config directly (`./package-apk.sh vulkan release`);
it defaults to `debug`, so existing callers are unaffected.

Dependency check — downloads missing dependencies before building
(25 November 2026): `Linux/cmake-build-android.sh` (and
`Linux/cmake-build.sh` for the Linux desktop build) now check that
every dependency the build needs is present, and download any that
are missing, BEFORE building — and therefore before packaging — for
debug and release alike. Android checks: `cmake`, `make`, `curl`, a
JDK 17+ (Temurin, from `JAVA_HOME`, any `jdk-17*` dir in the SDK
tree, or `javac` on PATH), the SDK (cmdline tools 11076708,
build-tools 34.0.0 with `aapt2`/`zipalign`/`apksigner`/`d8`, a
platform `android-XX` with `android.jar`), the NDK (r27c
27.2.12479018 by default; r30 also works), and an archiver
(`zip`/`7z`/`7za`/`wzzip`, same resolution order as
`package-apk.sh`, checked here as a pre-flight so a missing archiver
fails before the build instead of after it). Downloads come from
`dl.google.com` with `curl` (the SDK/NDK zips) and `api.adoptium.net`
for the JDK — `sdkmanager` does not work behind this VM's egress
proxy, so it is never used. A missing NDK is downloaded as r27c and
installed to `ndk/27.2.12479018`; a missing JDK is installed to
`jdk-17.0.21.1+2` (the name `package-apk.sh` looks for) and
`JAVA_HOME` is exported for the packaging step. On Windows (Git
Bash) the apt steps are skipped — only the SDK/NDK/JDK/tool checks
apply — and archives are unpacked with `unzip` when present, else
PowerShell `Expand-Archive`. A required dependency that cannot be
downloaded stops the build with the exact fix (command or URL);
`platform-tools` (adb, only needed to install on a device) and
`glslangValidator` only warn. When everything is present the check
is a silent no-op. The Linux desktop script does the same for its
dependencies with `apt` (OpenGL/X11/GLEW/GLU headers, Vulkan SDK +
xcb + X11-xcb for the Vulkan renderer, shaderc, `glslangValidator`,
ALSA, FFmpeg from the distro `-dev` packages — Ubuntu 24.04: FFmpeg
6.1.1, verified to satisfy the build — and eSpeak-ng); GLM and
miniaudio are vendored in `include/` and are never downloaded. See
`../scope/DEPENDENCY_CHECK_VERIFICATION.md` for the full dependency
list, the download sources and the test results.

`classes.dex` is built automatically, and the APK can be installed on a
device (25 November 2026): the build pipeline (`Linux/cmake-build-android.sh`)
now builds `classes.dex` (the Java TTS JNI helper, `Android/build-dex.sh`)
before packaging, so the APK always carries it — a failed dex build stops
the build before packaging, and `Android/package-apk.sh` does the same
for a standalone run (building the dex when it is missing, and failing
loudly with no APK produced when the build fails). The old "no
`classes.dex` — TTS JNI helper not packaged" note is gone: a missing dex
is an error now, not a note, because the manifest has
`android:hasCode=true` and TTS cannot work on Android without it.

After the APK is packaged, an interactive build offers to install it
on a connected device (`adb` — USB hardware or an emulator, both
appear in `adb devices`): one device is offered by name, several are
listed with serial + model and picked by number, and the install runs
`adb -s <serial> install -r <apk>` (`-r` so a reinstall keeps the
app's data). Flags: `-install` installs without prompting when a
device is present (exactly one device → it; several → the first
listed); `-no-install` skips the prompt entirely. A non-interactive
run (CI, a pipe) skips the prompt with a one-line note naming the
manual command; no devices, a missing adb, a decline, or a failed
install are all reported and never fail the build. `adb` is resolved
from the SDK `platform-tools` dir first, then from PATH, and
`CPGE_ADB` overrides both (a path to an `adb` binary). See
`../scope/DEX_INSTALL_VERIFICATION.md` for the test results.

Build outputs do not belong on GitHub (25 November 2026): `.gitignore`
covers `build/` and `build-android/` at any depth, `Debug/` /
`Release/`, and the binary extensions (`*.so`,
`*.cso`, `*.apk`, `*.dex`, …) plus `cache.dat` and `Assets/AIHistory/`,
so the `.so`, APKs, dex, keystore and staged copies above are never
committed. Tracked build binaries are being deleted from Phase2 in the
same cleanup (see `ReleaseInfo.md` and
`../scope/BINARY_DELETION_BATCH.md` for the list); vendored
third-party dependencies (`lib/`, `steam/lib/`, `D3D12/*.dll`) stay
tracked by design.

## Status (24 Nov 2026, Phase D step 3)

- Both Android `libCPGE.so` files link clean: OpenGL 79,712,272 B
  (`android-opengl-build-10.log`), Vulkan 81,433,672 B
  (`android-vulkan-build-10.log`), both exit 0.
- **APKs exist now, packaged by hand with aapt2** (step 3):
  `../scope/apk/cpge-opengl-debug.apk` (182,678,584 B) and
  `../scope/apk/cpge-vulkan-debug.apk` (184,398,904 B) — signed debug
  APKs (`apksigner verify`: v2 + v3 schemes), each with
  `lib/arm64-v8a/libCPGE.so` (stored uncompressed), the full `Assets/`
  tree minus `AIHistory`, and `classes.dex` (the TTS helper). Packaged
  by `../scope/apk/package-apk.sh` (manifest via `aapt2 link`, align,
  sign with a local debug keystore). The packaging scripts moved into
  the repo as `Android/package-apk.sh` / `Android/build-dex.sh` on
  25 November 2026; new APKs land in `Android/`. See the step-3
  verification record for the full breakdown. They have never been
  installed — no device.
- Gradle still cannot run in this VM (its client↔daemon loopback TCP
  is intercepted by the sandbox network policy). Daniel decided
  24 November 2026 that manual aapt2 packaging is the primary path and
  Gradle is the fallback only — do not wait on the `other_tcp` setting.
- Assets: the APKs already carry the repo's `Assets/` tree, and
  `Linux/cmake-build-android.sh` now syncs `Assets/` to Gradle's
  `src/main/assets/` on every successful build (25 November 2026;
  `AIHistory` excluded). The engine reads APK assets through
  `AAssetManager` in `FileIO.cpp`, and `GameConfig.cfg` / saves /
  `DebugLog.txt` live in internal storage, not assets.
- TTS-via-JNI landed in step 3 (see `CpgeTts.java` above); the FFmpeg
  decision is still open — see
  `../scope/PHASE_D_STEP3_FFMPEG_ANALYSIS.md` (options + recommendation
  for Daniel; nothing vendored).

## Notes

- `minSdk 26`: AAudio, used by the miniaudio sound backend, needs API 26.
  Keep in sync with `Linux/cmake-build-android.sh` (`ANDROID_PLATFORM`
  default `android-26`).
- Package name `com.ultimanium.cpge` is provisional.
- `env.sh` note: `~/workspace/android-sdk/env.sh` still points `JAVA_HOME`
  at the JRE; updating it to the JDK above is pending Daniel's go-ahead (its
  read tripped an execution-risk check during step 2). Gradle does not need
  it (pinned in `gradle.properties`); anything else that needs the JDK should
  export `JAVA_HOME` to the JDK path by hand until then.
