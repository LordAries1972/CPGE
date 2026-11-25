#!/usr/bin/env bash
# cmake-build-android.sh  <renderer> [debug|release] [abi]
#   renderer : opengl | vulkan
#   config   : debug | release  (default: debug)
#   abi      : arm64-v8a | x86_64 | armeabi-v7a | x86  (default: arm64-v8a)
#
# Options (any position):
#   -clean      fresh compile (wipe the pipeline's build tree first)
#   -warnings  show compiler warnings (off by default — builds are quiet)
#
# Requires ANDROID_NDK (or ANDROID_NDK_HOME / ANDROID_HOME) environment variable
# pointing to the root of the Android NDK.
#
# Examples:
#   ./cmake-build-android.sh opengl
#   ./cmake-build-android.sh vulkan release arm64-v8a
#   ./cmake-build-android.sh vulkan debug -warnings   # show compiler warnings
#   ./cmake-build-android.sh clean
#   ./cmake-build-android.sh help       # lists every directive (also -h, --help, -help)
#
# On success the built libCPGE.so is installed to the project root, to the
# Gradle project's jniLibs/<abi>/, and Assets/ is synced to the Gradle
# project's assets/; the AndroidManifest.xml NativeActivity entry is
# verified; then the APK is built with aapt2 (see "What a build does" in
# the help). Release builds are stripped of debug info (llvm-strip) and
# are not debuggable; debug builds keep debug info and are debuggable.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_EXE="${CMAKE_EXE:-cmake}"

# Split the command line into the positional arguments and the optional
# switches -clean / -warnings (any position).
# Without -clean the existing build tree (objects, CMake cache, binaries) is reused, so only changed sources recompile.
# Without -warnings compiler warnings are suppressed (quiet build, -w); with
# it they are shown (the behaviour before -warnings existed).
CLEAN_BUILD=""
SHOW_WARNINGS=""
POSARGS=()
for a in "$@"; do
    case "${a,,}" in
        -clean) CLEAN_BUILD=1 ;;
        -warnings) SHOW_WARNINGS=1 ;;
        *) POSARGS+=("$a") ;;
    esac
done
ARG1="${POSARGS[0]:-}"
ARG2="${POSARGS[1]:-}"
ARG3="${POSARGS[2]:-}"

# --- Help (lists every directive and exits) ---
show_help() {
    local me
    me="$(basename "$0")"
    cat <<EOF

${me} - cross-compile the engine for Android with CMake + the Android NDK

Usage:
  ${me} <directive> [config] [abi] [-clean] [-warnings]

Build directives (one render pipeline):
  opengl    OpenGL ES renderer
  vulkan    Vulkan renderer
            (DirectX 11/12 are Windows-only - use cmake-build.bat)

Options (any position, used by every build directive):
  -clean    Deletes the build tree of the pipeline being built (objects, CMake
            cache, binaries) first for a fresh compile. Without it the existing
            binaries are reused (incremental build).
  -warnings
            Shows compiler warnings. Builds are quiet by default: warnings
            are suppressed (the compiler is passed -w), so only errors and
            the build summary are shown. Pass -warnings to see every
            warning, as on Windows (which has no such flag — warnings
            are always shown there).

Maintenance directives:
  clean     Deletes the Android build directory (Linux/build-android).
  help      Shows this list. Aliases: -h  --help  -help

Config (second argument, used by opengl / vulkan):
  debug     Debug build   (default when omitted)
  release   Release build

ABI (third argument, used by opengl / vulkan):
  arm64-v8a      64-bit ARM   (default when omitted)
  x86_64         64-bit x86   (emulators)
  armeabi-v7a    32-bit ARM
  x86            32-bit x86   (emulators)

What a build does:
  1. Locates the Android NDK (see Environment below).
  2. Configures with the NDK toolchain (c++_shared STL) and builds into
     Linux/build-android/<abi>/<Renderer>/<Config> using all CPU cores.
     (Includes.h is NOT patched: the renderer define is passed by CMake,
     which is the single source of truth.)
  3. On success, installs libCPGE.so to the project root (the executable
     placed in the root folder — the same principle as the Windows
     install step) and to Android/app/src/main/jniLibs/<abi>/, syncs
     Assets/ to Android/app/src/main/assets/ (AIHistory excluded), and
     verifies the AndroidManifest.xml NativeActivity entry (see below).
  4. Builds the APK with aapt2 (Android/package-apk.sh), AFTER the
     install, so the APK packages the .so and Assets/ — debug AND
     release, both as cpge-<renderer>-<config>.apk (arm64-v8a only;
     skipped with a clear note for another ABI). Release builds are
     stripped of debug info with llvm-strip (from the NDK) and are
     not debuggable; debug builds keep debug info and are debuggable.
     A release APK is signed with the release keystore when
     CPGE_RELEASE_KEYSTORE is set (see Android/package-apk.sh);
     otherwise it is signed with the debug keystore and the script
     says so.

NativeActivity — what it is (in plain words):
  Android apps normally start in a Java activity. NativeActivity is
  Android's built-in activity that starts the native .so directly,
  with no Java activity code of our own: the manifest names
  android.app.NativeActivity and the meta-data android.app.lib_name
  tells it which library to load (CPGE, i.e. libCPGE.so). The
  MAIN/LAUNCHER intent-filter makes the app appear in the launcher.
  GameActivity is the newer Jetpack alternative — this project uses
  NativeActivity. The build checks this entry and repairs it if it
  is missing or wrong; a correct manifest is left byte-identical.

Environment:
  ANDROID_NDK        Android NDK root (checked first)
  ANDROID_NDK_HOME   Android NDK root (checked second)
  ANDROID_HOME       Android SDK root; uses ndk-bundle/ or the newest ndk/<ver>
  ANDROID_PLATFORM   Android API level (default: android-26)
  GLM_ROOT           Override only: folder containing glm/ (GLM headers) to
                     use instead of the vendored copy in include/ (version
                     0.9.9.8, managed by Daniel). Normally unset — GLM is
                     vendored and found automatically on every host.
  CMAKE_EXE          Path to the cmake binary (default: cmake on PATH)

Examples:
  ${me} opengl
  ${me} vulkan release
  ${me} vulkan release arm64-v8a
  ${me} opengl debug x86_64
  ${me} vulkan release -clean   # fresh full compile
  ${me} vulkan debug -warnings  # show compiler warnings
  ${me} clean
  ${me} help

EOF
}

# Help is checked first (any argument position), so it works even when
# cmake / the NDK is not installed — same as cmake-build.bat.
for _a in "$@"; do
    case "${_a,,}" in
        help|-h|--help|-help)
            show_help
            exit 0
            ;;
    esac
done

# --- NativeActivity manifest entry -----------------------------------------
# Checks the AndroidManifest.xml NativeActivity entry and repairs it
# (idempotently) if it is missing or wrong; a correct manifest is left
# byte-identical. See the help text for what NativeActivity means.
manifest_has_native_activity() {
    local _mf="$1"
    grep -q 'android:name="android.app.NativeActivity"' "${_mf}" \
        && grep -q 'android:name="android.app.lib_name" android:value="CPGE"' "${_mf}" \
        && grep -q 'android.intent.action.MAIN' "${_mf}" \
        && grep -q 'android.intent.category.LAUNCHER' "${_mf}"
}

ensure_native_activity() {
    local _mf="${PROJECT_ROOT}/Android/app/src/main/AndroidManifest.xml"
    if [[ ! -f "${_mf}" ]]; then
        echo "ERROR: AndroidManifest.xml not found: ${_mf}"
        return 1
    fi
    if manifest_has_native_activity "${_mf}"; then
        echo "AndroidManifest.xml: NativeActivity entry OK (android.app.NativeActivity, lib_name=CPGE, MAIN/LAUNCHER)."
        return 0
    fi
    echo "AndroidManifest.xml: NativeActivity entry missing or wrong — repairing..."
    if ! command -v python3 &>/dev/null; then
        echo "ERROR: python3 is required to repair the manifest and was not found."
        echo "Add the android.app.NativeActivity entry by hand (lib_name CPGE, MAIN/LAUNCHER intent-filter)."
        return 1
    fi
    python3 - "${_mf}" <<'PYEOF'
import re, sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
orig = text
activity_re = re.compile(r'<activity\b[^>]*android:name="android\.app\.NativeActivity"[^>]*>(.*?)</activity>', re.S)
m = activity_re.search(text)
if m is None:
    block = ('        <activity\n'
             '            android:name="android.app.NativeActivity"\n'
             '            android:label="CPGE"\n'
             '            android:configChanges="orientation|keyboardHidden|screenSize|screenLayout|smallestScreenSize|uiMode"\n'
             '            android:launchMode="singleTask"\n'
             '            android:screenOrientation="landscape">\n'
             '            <meta-data android:name="android.app.lib_name" android:value="CPGE" />\n'
             '            <intent-filter>\n'
             '                <action android:name="android.intent.action.MAIN" />\n'
             '                <category android:name="android.intent.category.LAUNCHER" />\n'
             '            </intent-filter>\n'
             '        </activity>\n')
    if '</application>' not in text:
        print('ERROR: no </application> tag in manifest', file=sys.stderr)
        sys.exit(1)
    text = text.replace('</application>', block + '    </application>', 1)
else:
    body = m.group(1)
    if 'android.app.lib_name' in body:
        body = re.sub(r'(android:name="android\.app\.lib_name"\s+android:value=")[^"]*(")', r'\1CPGE\2', body)
    else:
        body = '            <meta-data android:name="android.app.lib_name" android:value="CPGE" />\n' + body
    if 'android.intent.action.MAIN' not in body or 'android.intent.category.LAUNCHER' not in body:
        body += ('            <intent-filter>\n'
                 '                <action android:name="android.intent.action.MAIN" />\n'
                 '                <category android:name="android.intent.category.LAUNCHER" />\n'
                 '            </intent-filter>\n')
    text = text[:m.start(1)] + body + text[m.end(1):]
if text != orig:
    open(path, 'w', encoding='utf-8').write(text)
    print('manifest repaired')
else:
    print('manifest already correct')
PYEOF
    if manifest_has_native_activity "${_mf}"; then
        echo "AndroidManifest.xml: NativeActivity entry repaired and verified."
        return 0
    fi
    echo "ERROR: AndroidManifest.xml repair did not produce a valid NativeActivity entry."
    return 1
}

# --- Path / tool helpers (Windows/Git Bash) --------------------------------
# Same helpers as Android/package-apk.sh: posix_path normalises a root
# with cygpath when it exists (Git Bash/MSYS2/Cygwin; no-op on Linux),
# find_tool resolves a tool through its plain/.exe/.bat/.cmd names.
posix_path() {  # posix_path <path> -> POSIX form (no-op without cygpath)
    if command -v cygpath &>/dev/null; then
        cygpath -u "$1"
    else
        printf '%s' "$1"
    fi
}

find_tool() {  # find_tool <dir> <name> -> prints the resolved path or nothing
    local _dir="$1" _name="$2" _suffix
    for _suffix in "" ".exe" ".bat" ".cmd"; do
        if [[ -f "$_dir/$_name$_suffix" ]]; then
            printf '%s' "$_dir/$_name$_suffix"
            return 0
        fi
    done
    return 1
}

# llvm-strip from the NDK: $NDK_ROOT/toolchains/llvm/prebuilt/*/bin
# (one dir per host OS — globbed, so it works on Linux and on
# Windows/Git Bash). Prints the path or nothing; always returns 0 so
# callers can test for empty. NDK_ROOT is resolved later in this script
# (this function is only called from install_and_package_android, after
# the NDK check).
find_llvm_strip() {
    local _ndk_posix _pb _ls
    _ndk_posix="$(posix_path "${NDK_ROOT:-}")"
    for _pb in "$_ndk_posix"/toolchains/llvm/prebuilt/*/; do
        [[ -d "$_pb" ]] || continue
        _ls="$(find_tool "${_pb}bin" llvm-strip || true)"
        if [[ -n "$_ls" ]]; then
            printf '%s' "$_ls"
            return 0
        fi
    done
    return 0
}

# --- Install to root + Gradle project, then package the APK -----------------
# Runs on success only. Automates the three "Next steps" this script used
# to print, in order, and then builds the APK AFTER the install so the APK
# packages the root .so and Assets/:
#   1. libCPGE.so to the project root and to jniLibs/<abi>/.
#   2. Assets/ to the Gradle project's assets/ (AIHistory excluded).
#   3. NativeActivity entry in AndroidManifest.xml (verified, repaired
#      if missing or wrong — see ensure_native_activity above).
#   4. APK via Android/package-apk.sh (aapt2), debug and release,
#      arm64-v8a only; skipped with a clear note for another ABI.
#      Release builds are stripped (llvm-strip) before packaging.
install_and_package_android() {
    local _so="${BUILD_DIR}/libCPGE.so"
    if [[ ! -f "${_so}" ]]; then
        echo "ERROR: expected library not found after a successful build: ${_so}"
        return 1
    fi

    # 1. Project root (the executable placed in the root folder — same
    #    principle as the Windows install step) and the Gradle jniLibs.
    cp -f "${_so}" "${PROJECT_ROOT}/libCPGE.so" || { echo "ERROR: could not install libCPGE.so to the project root"; return 1; }
    echo "Installed: ${PROJECT_ROOT}/libCPGE.so"
    local _jni="${PROJECT_ROOT}/Android/app/src/main/jniLibs/${ABI}"
    mkdir -p "${_jni}"
    cp -f "${_so}" "${_jni}/libCPGE.so" || { echo "ERROR: could not install libCPGE.so to ${_jni}"; return 1; }
    echo "Installed: ${_jni}/libCPGE.so"

    # 1b. Release: strip debug info from BOTH installed copies (root and
    #     jniLibs). Debug keeps its symbols. llvm-strip comes from the
    #     NDK; a missing llvm-strip is a hard error for release builds —
    #     a release build must never ship with debug info.
    if [[ "${CONFIG}" == "Release" ]]; then
        local _llvm_strip _f _before _after
        _llvm_strip="$(find_llvm_strip)"
        if [[ -z "${_llvm_strip}" ]]; then
            echo "ERROR: llvm-strip not found in ${NDK_ROOT}/toolchains/llvm/prebuilt/*/bin — release builds must be stripped."
            echo "  Install the NDK via Android Studio (SDK Manager → SDK Tools → NDK (Side by side)) or: sdkmanager \"ndk;<version>\""
            return 1
        fi
        for _f in "${PROJECT_ROOT}/libCPGE.so" "${_jni}/libCPGE.so"; do
            _before="$(stat -c%s "$_f")"
            "$_llvm_strip" "$_f" || { echo "ERROR: llvm-strip failed on $_f"; return 1; }
            _after="$(stat -c%s "$_f")"
            echo "Stripped: ${_f} (${_before} -> ${_after} bytes)"
        done
    fi

    # 2. Assets/ to the Gradle project's assets/ (AIHistory excluded,
    #    exactly as Android/package-apk.sh does for the APK).
    local _assets="${PROJECT_ROOT}/Android/app/src/main/assets"
    mkdir -p "${_assets}"
    if command -v rsync &>/dev/null; then
        rsync -a --delete --exclude 'AIHistory' "${PROJECT_ROOT}/Assets/" "${_assets}/" || { echo "ERROR: could not sync Assets/ to ${_assets}"; return 1; }
    else
        echo "NOTE: rsync not found — falling back to cp -r for Assets/."
        rm -rf "${_assets}"
        mkdir -p "${_assets}"
        cp -r "${PROJECT_ROOT}/Assets/." "${_assets}/" || { echo "ERROR: could not copy Assets/ to ${_assets}"; return 1; }
        rm -rf "${_assets}/AIHistory"
    fi
    echo "Assets synced: ${_assets}/ (AIHistory excluded)"

    # 3. NativeActivity entry in the manifest.
    ensure_native_activity || return 1

    # 4. APK, built AFTER the install so it packages the .so and
    #    Assets/. Debug and release both package (arm64-v8a only);
    #    the config is passed through so package-apk.sh strips the
    #    release .so and leaves the debug one alone.
    local _apk_script="${PROJECT_ROOT}/Android/package-apk.sh"
    if [[ "${ABI}" == "arm64-v8a" ]]; then
        if [[ -x "${_apk_script}" ]]; then
            echo "Packaging APK with aapt2: ${_apk_script} ${ARG1,,} ${CONFIG,,}"
            if "${_apk_script}" "${ARG1,,}" "${CONFIG,,}"; then
                echo "APK packaged: Android/cpge-${ARG1,,}-${CONFIG,,}.apk"
            else
                echo "ERROR: APK packaging failed."
                echo "  Library build: SUCCEEDED (libCPGE.so installed to the project root and jniLibs/${ABI}/)."
                echo "  APK packaging: FAILED (no new APK produced)."
                return 1
            fi
        else
            # The packaging script ships in the repo — its absence means a
            # broken checkout, not a missing optional tool, so fail loudly.
            echo "ERROR: APK packaging script missing: ${_apk_script}"
            echo "  It ships in the repo at Android/package-apk.sh — your checkout is incomplete."
            echo "  Library build: SUCCEEDED (libCPGE.so installed to the project root and jniLibs/${ABI}/)."
            return 1
        fi
    else
        echo "NOTE: APK packaging skipped — aapt2 packaging is arm64-v8a only (this build: ${CONFIG}/${ABI})."
        echo "  Library build: SUCCEEDED (libCPGE.so installed to the project root and jniLibs/${ABI}/)."
    fi
    return 0
}

# --- Clean ---
if [[ "${ARG1,,}" == "clean" ]]; then
    echo "Cleaning Android build directories..."
    rm -rf "${SCRIPT_DIR}/build-android"
    echo "Clean complete."
    exit 0
fi

# --- Usage ---
if [[ -z "${ARG1}" ]]; then
    echo ""
    echo "Usage: $(basename "$0") <renderer> [debug|release] [abi] [-clean] [-warnings]"
    echo "  Renderers : opengl  vulkan"
    echo "  Config    : debug  release        (default: debug)"
    echo "  ABI       : arm64-v8a  x86_64  armeabi-v7a  x86  (default: arm64-v8a)"
    echo "  -warnings : show compiler warnings (off by default — builds are quiet)"
    echo ""
    echo "Environment variables:"
    echo "  ANDROID_NDK        Path to the Android NDK root"
    echo "  ANDROID_PLATFORM   Android API level (default: android-26)"
    echo "  GLM_ROOT           Override only: folder containing glm/ to use instead"
    echo "                     of the vendored copy in include/ (normally unset —"
    echo "                     GLM is vendored and found automatically)"
    echo "  CMAKE_EXE          Path to cmake binary (default: cmake)"
    echo ""
    echo "Examples:"
    echo "  $(basename "$0") opengl"
    echo "  $(basename "$0") vulkan release arm64-v8a"
    echo "  $(basename "$0") vulkan debug -warnings"
    echo "  $(basename "$0") clean"
    echo ""
    echo "Run '$(basename "$0") help' for the full list of directives."
    exit 1
fi

# --- Locate Android NDK ---
# Newest NDK install below a given SDK ndk/ directory. Directories only:
# a stray file (e.g. a downloaded NDK .zip) must not be picked up as the
# "newest NDK" — that is what happened when android-ndk-r30-linux.zip sat
# next to the NDK installs and sort -V picked the .zip over the dirs.
newest_ndk_dir() {
    local _base="$1"
    ls -d "${_base}"/*/ 2>/dev/null | sort -V | tail -1
}

# Last candidate is the Phase D install location (~/workspace/android-sdk,
# NDK r27c) so the script works on the build machine without any env vars;
# source ~/workspace/android-sdk/env.sh for the explicit ANDROID_HOME form.
NDK_ROOT=""
for _candidate in \
        "${ANDROID_NDK:-}" \
        "${ANDROID_NDK_HOME:-}" \
        "${ANDROID_HOME:-}/ndk-bundle" \
        "$(newest_ndk_dir "${ANDROID_HOME:-/nonexistent}/ndk")" \
        "$(newest_ndk_dir "${HOME}/workspace/android-sdk/ndk")"; do
    if [[ -n "${_candidate}" && -d "${_candidate}" && -f "${_candidate}/build/cmake/android.toolchain.cmake" ]]; then
        NDK_ROOT="${_candidate}"
        break
    fi
done

if [[ -z "${NDK_ROOT}" ]]; then
    echo "ERROR: Android NDK not found."
    echo ""
    echo "Set one of the following environment variables to the NDK root directory:"
    echo "  export ANDROID_NDK=/path/to/ndk/<version>"
    echo "  export ANDROID_NDK_HOME=/path/to/ndk/<version>"
    echo "  export ANDROID_HOME=/path/to/sdk   (ndk-bundle/ or ndk/<ver> must exist)"
    echo ""
    echo "Install the NDK via Android Studio:"
    echo "  SDK Manager → SDK Tools → NDK (Side by side)"
    echo "Or via command line:"
    echo "  sdkmanager 'ndk;<version>'"
    exit 1
fi
echo "Android NDK : ${NDK_ROOT}"

# --- Map renderer ---
RENDERER=""
case "${ARG1,,}" in
    opengl)
        RENDERER="OpenGL"
        ;;
    vulkan)
        RENDERER="Vulkan"
        ;;
    *)
        echo "ERROR: Unknown renderer '${ARG1}'"
        echo "Valid renderers: opengl  vulkan"
        exit 1
        ;;
esac

# --- Config ---
CONFIG="Debug"
case "${ARG2,,}" in
    release) CONFIG="Release" ;;
    debug|"") CONFIG="Debug" ;;
    *)
        echo "ERROR: Unknown config '${ARG2}' — use debug or release"
        exit 1
        ;;
esac

# --- ABI ---
ABI="${ARG3:-arm64-v8a}"
case "${ABI}" in
    arm64-v8a|x86_64|armeabi-v7a|x86) ;;
    *)
        echo "ERROR: Unknown ABI '${ABI}'"
        echo "Valid ABIs: arm64-v8a  x86_64  armeabi-v7a  x86"
        exit 1
        ;;
esac

# --- Android API level ---
# Default android-26: the manifest's minSdkVersion is 26 (AAudio, used by the
# miniaudio backend, needs API 26), so the NDK build targets the same level.
# Override with ANDROID_PLATFORM=android-34 for a newer target.
ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-26}"

# --- Validate cmake ---
if ! command -v "${CMAKE_EXE}" &>/dev/null; then
    echo "ERROR: cmake not found."
    echo "Install it via Android Studio's bundled cmake, or:"
    echo "  sdkmanager 'cmake;<version>'"
    exit 1
fi

# --- Warnings ---
# Default: quiet build. Linux/CMakeLists.txt adds -Wall, so pass -w through
# CMAKE_CXX_FLAGS to suppress every compiler warning (verified on this
# toolchain: -w placed before -Wall still suppresses, on GCC and on the
# NDK Clang). With -warnings the flag is omitted and warnings are shown
# as before. The -D is passed on every configure so switching between
# quiet and -warnings on the same build tree takes effect
# (CMAKE_CXX_FLAGS is cached by CMake).
if [[ -n "${SHOW_WARNINGS}" ]]; then
    CMAKE_CXX_FLAGS_ARG="-DCMAKE_CXX_FLAGS=${CXXFLAGS:-}"
else
    CMAKE_CXX_FLAGS_ARG="-DCMAKE_CXX_FLAGS=${CXXFLAGS:+$CXXFLAGS }-w"
fi

# --- Renderer define -------------------------------------------------------
# NOTE: Includes.h is NOT patched here. The renderer define (__USE_OPENGL__ /
# __USE_VULKAN__) is passed to CMake via -DRENDERER by Linux/CMakeLists.txt.
# An earlier version of this script regex-patched Includes.h globally, which
# touched the defines in every platform section (Linux, Android, macOS,
# Windows) — that patching was removed; CMake is the single source of truth.

# --- Build ---
BUILD_DIR="${SCRIPT_DIR}/build-android/${ABI}/${RENDERER}/${CONFIG}"
# -clean: wipe this pipeline's build tree so the compile below starts completely fresh.
if [[ -n "${CLEAN_BUILD}" && -d "${BUILD_DIR}" ]]; then
    echo "-clean: removing ${BUILD_DIR} ..."
    rm -rf "${BUILD_DIR}"
    if [[ -d "${BUILD_DIR}" ]]; then echo "ERROR: Could not remove ${BUILD_DIR}"; exit 1; fi
fi
mkdir -p "${BUILD_DIR}"

echo ""
echo "Building: OS=Android  ABI=${ABI}  Renderer=${RENDERER}  Config=${CONFIG}  API=${ANDROID_PLATFORM}"
echo "          Dir=${BUILD_DIR}"
echo ""

"${CMAKE_EXE}" \
    -S "${SCRIPT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${NDK_ROOT}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM="${ANDROID_PLATFORM}" \
    -DANDROID_STL="c++_shared" \
    -DRENDERER:STRING="${RENDERER}" \
    -DCMAKE_BUILD_TYPE="${CONFIG}" \
    "${CMAKE_CXX_FLAGS_ARG}"

"${CMAKE_EXE}" --build "${BUILD_DIR}" --parallel "$(nproc 2>/dev/null || echo 4)"

echo ""
echo "Build succeeded: Android/${ABI} ${RENDERER} ${CONFIG} -- $(date)"
echo ""

# --- Install to root + Gradle project, then package the APK ---
install_and_package_android || exit 1
