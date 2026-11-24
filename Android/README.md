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
- `app/src/main/java/com/ultimanium/cpge/CpgeTts.java` — the Java helper
  for Android TTS (Phase D step 3): a thin wrapper over
  `android.speech.tts.TextToSpeech`, driven through JNI from the native
  `TTSManager`. The manifest no longer declares `hasCode="false"`, so
  the APK carries `classes.dex` for it (built by
  `../scope/apk/build-dex.sh`; Gradle picks the sources up from the
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
  sign with a local debug keystore). See the step-3 verification record
  for the full breakdown. They have never been installed — no device.
- Gradle still cannot run in this VM (its client↔daemon loopback TCP
  is intercepted by the sandbox network policy). Daniel decided
  24 November 2026 that manual aapt2 packaging is the primary path and
  Gradle is the fallback only — do not wait on the `other_tcp` setting.
- Assets: the APKs already carry the repo's `Assets/` tree (Gradle
  `src/main/assets/` — not wired yet; the engine reads APK assets
  through `AAssetManager` in `FileIO.cpp`, and `GameConfig.cfg` /
  saves / `DebugLog.txt` live in internal storage, not assets).
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
