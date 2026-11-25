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
# pointing to the root of the Android NDK. When the NDK (or any other
# dependency) is not found, the script downloads it — see
# check_android_deps below. Before building (debug and release alike)
# the script checks that every dependency is present and downloads any
# that are missing, so a missing dependency can never fail the build or
# the packaging step halfway through.
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
# verified; classes.dex (the Java TTS JNI helper) is built with
# Android/build-dex.sh; then the APK is built with aapt2 (see "What a
# build does" in the help). On a clean release build the high scores and
# recordings are reset first (Linux/reset.sh, before the Assets/ sync),
# so the APK ships a fresh start — see the help. After packaging, an
# interactive session is offered to install the APK on a connected device
# (adb — USB hardware or an emulator); -install installs without prompting,
# -no-install skips the prompt. Release builds are stripped of debug info
# (llvm-strip) and are not debuggable; debug builds keep debug info
# and are debuggable.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_EXE="${CMAKE_EXE:-cmake}"

# Split the command line into the positional arguments and the optional
# switches -clean / -warnings / -install / -no-install (any position).
# Without -clean the existing build tree (objects, CMake cache, binaries) is reused, so only changed sources recompile.
# Without -warnings compiler warnings are suppressed (quiet build, -w); with
# it they are shown (the behaviour before -warnings existed).
CLEAN_BUILD=""
SHOW_WARNINGS=""
INSTALL_APK=""
NO_INSTALL=""
POSARGS=()
for a in "$@"; do
    case "${a,,}" in
        -clean) CLEAN_BUILD=1 ;;
        -warnings) SHOW_WARNINGS=1 ;;
        -install) INSTALL_APK=1 ;;
        -no-install) NO_INSTALL=1 ;;
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
  -install
            Installs the APK on a connected device after packaging,
            without prompting (exactly one device → it; several →
            the first listed). No device → a note, and the build
            still succeeds.
  -no-install
            Skips the device-install prompt entirely. Wins over
            -install when both are given.

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
  0. Checks every dependency the build needs (cmake, make, curl, a
     JDK 17+, the Android SDK — cmdline tools, build-tools with
     aapt2/zipalign/apksigner/d8, a platform android-XX — the NDK, and
     an archiver: zip/7z/7za/wzzip) and downloads any that are missing
     (curl from dl.google.com for the SDK/NDK, api.adoptium.net for
     the Temurin JDK 17). This runs before building, for debug and
     release alike. A required dependency that cannot be downloaded
     stops the build here with the exact fix; an optional one
     (platform-tools/adb, glslangValidator) only warns. When
     everything is present this step is a silent no-op.
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
  4. Builds classes.dex (the Java TTS JNI helper, Android/build-dex.sh)
     and then the APK with aapt2 (Android/package-apk.sh), AFTER the
     install, so the APK packages the .so, Assets/ and the dex — debug
     AND release, both as cpge-<renderer>-<config>.apk (arm64-v8a only;
     skipped with a clear note for another ABI). A failed dex build
     stops the build here, before packaging — the APK is never
     packaged without classes.dex. Release builds are stripped of
     debug info with llvm-strip (from the NDK) and are not debuggable;
     debug builds keep debug info and are debuggable. A release APK
     is signed with the release keystore when CPGE_RELEASE_KEYSTORE
     is set (see Android/package-apk.sh); otherwise it is signed with
     the debug keystore and the script says so.
  5. Offers to install the APK on a connected device (adb — USB
     hardware or an emulator, both appear in \`adb devices\`). Only
     when stdin is a terminal; a non-interactive run skips the prompt
     with a one-line note naming the manual command. -install installs
     without prompting when a device is present (exactly one device
     → it; several → the first listed); -no-install skips the prompt
     entirely. The install never fails the build.

  Reset on a clean release build (25 November 2026): when BOTH -clean
  and release were requested, the build resets the high scores and
  their recordings for a fresh start (Linux/reset.sh) BEFORE the
  Assets/ sync in step 3, so the APK ships a fresh table — and clears
  the same files on every connected device (best effort, via adb
  run-as; never fails the build — see reset_android_device below).
  A failed build-machine reset stops the build here, before
  packaging: a clean release build must never ship with stale
  scores. Debug builds and builds without -clean never reset.

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

# --- Dependency check: download if not found (25 November 2026) -------------
# Before building (and therefore before packaging, for both debug and
# release — this check is NOT gated on CONFIG), every dependency the
# Android build needs is verified present, and downloaded when it is
# missing. This is the "repair" step: a fresh machine (or a wiped VM)
# must be able to go from checkout to APK without the user hunting
# down SDK pieces by hand.
#
# HARD (the build or the APK packaging cannot succeed without them):
# cmake, make, curl (for the downloads), a JDK 17+ (javac + keytool),
# the Android SDK (cmdline tools, build-tools with aapt2 / zipalign /
# apksigner / d8, a platform android-XX with android.jar), the NDK,
# and an archiver (zip/7z/7za/wzzip — the same resolution order as
# Android/package-apk.sh, checked here as a pre-flight so a missing
# archiver fails before the build instead of after it). If one is
# missing and cannot be downloaded, the script prints the exact fix
# and exits non-zero BEFORE building.
# SOFT: platform-tools (adb — only needed to install on a device) and
# glslangValidator (Vulkan shaders fall back to a copy + runtime
# shaderc compile without it). A missing one is still downloaded when
# possible; if the download fails the script warns and builds on.
# GLM and miniaudio are VENDORED in repo/include/ and are never
# downloaded — Linux/CMakeLists.txt already fails fast with a clear
# message if include/glm/glm.hpp is missing.
#
# Downloads come from dl.google.com (curl — sdkmanager does not work
# behind this VM's egress proxy, its Java tunnelling fails) and from
# api.adoptium.net for the Temurin JDK 17. Every URL below was verified
# with curl -I on 25 November 2026: NDK r27c (27.2.12479018, the
# default), cmdline tools 11076708, build-tools r34, platform
# android-34 (platform-34-ext12_r01.zip), platform-tools, Temurin 17.
# On Windows (Git Bash) the apt steps are skipped — only the SDK/NDK/
# JDK/tool checks apply — and archives are unpacked with unzip when
# present, else PowerShell Expand-Archive. The SDK is laid out in the
# standard shape (cmdline-tools/latest, build-tools/34.0.0,
# platforms/android-34, ndk/27.2.12479018, jdk-17.0.21.1+2).
# When everything is present the check is a silent no-op and the build
# proceeds exactly as before.
_have_cmd() { command -v "$1" &>/dev/null; }   # _have_cmd <name>
_have_file() { [[ -f "$1" ]]; }                # _have_file <path>
_is_windows() {  # Git Bash / MSYS2 / Cygwin
    case "$(uname -s 2>/dev/null)" in MINGW*|MSYS*|CYGWIN*) return 0 ;; *) return 1 ;; esac
}
_is_linux() { [[ "$(uname -s 2>/dev/null)" == "Linux" ]]; }
_android_os() { _is_windows && echo windows || echo linux; }  # dl.google.com OS suffix

# _download <url> <dest-file> — curl, failing loudly on HTTP errors.
# Returns 1 when curl is missing or the download fails.
_download() {
    local _url="$1" _dest="$2"
    if ! _have_cmd curl; then
        echo "ERROR: curl is required to download dependencies and was not found."
        return 1
    fi
    echo "Dependency check: downloading $(basename "$_dest") from $_url ..."
    curl -fSL --max-time 1800 "$_url" -o "$_dest" || { echo "ERROR: download failed: $_url"; return 1; }
    return 0
}

# _unzip_to <zip> <dir> — unzip when present, else PowerShell
# Expand-Archive on Windows (Git Bash does not always ship unzip).
_unzip_to() {
    local _zip="$1" _dir="$2"
    if _have_cmd unzip; then
        unzip -q -o "$_zip" -d "$_dir" || return 1
        return 0
    fi
    if _is_windows && _have_cmd powershell.exe; then
        powershell.exe -NoProfile -Command "Expand-Archive -LiteralPath '$_zip' -DestinationPath '$_dir' -Force" || return 1
        return 0
    fi
    echo "ERROR: cannot unpack $_zip — neither unzip nor PowerShell Expand-Archive is available."
    return 1
}

# _install_archive <file> <destdir> — extract a .zip or .tar.gz into
# destdir. Handles both layouts: a single top-level directory (its
# contents move into destdir) or files at the top level (they move into
# destdir). The temp dir lives under the SDK root (same filesystem as
# the destination, so the moves are renames — fast even for the 664 MB
# NDK — and /tmp may be a small tmpfs that cannot hold it).
_install_archive() {
    local _file="$1" _dest="$2" _tmp _n _only
    _tmp="$(mktemp -d "${SDK_ROOT}/.cpge-dl-XXXXXX")" || return 1
    case "$_file" in
        *.zip) _unzip_to "$_file" "$_tmp" || { rm -rf "$_tmp"; return 1; } ;;
        *.tar.gz|*.tgz) tar -xzf "$_file" -C "$_tmp" || { rm -rf "$_tmp"; return 1; } ;;
        *) echo "ERROR: unknown archive type: $_file"; rm -rf "$_tmp"; return 1 ;;
    esac
    mkdir -p "$_dest" || { rm -rf "$_tmp"; return 1; }
    _n="$(find "$_tmp" -mindepth 1 -maxdepth 1 | wc -l)"
    if [[ "$_n" -eq 1 ]]; then
        _only="$(find "$_tmp" -mindepth 1 -maxdepth 1)"
        if [[ -d "$_only" ]]; then
            mv "$_only"/* "$_dest"/ 2>/dev/null || true
            mv "$_only"/.[!.]* "$_dest"/ 2>/dev/null || true
        else
            mv "$_only" "$_dest"/
        fi
    else
        mv "$_tmp"/* "$_dest"/ 2>/dev/null || true
        mv "$_tmp"/.[!.]* "$_dest"/ 2>/dev/null || true
    fi
    rm -rf "$_tmp"
    return 0
}

# apt helper (Linux only; kept in sync with Linux/cmake-build.sh).
# _apt_install_pkgs <pkg...> — installs the named packages with apt,
# with a dpkg fallback from the apt cache. Returns 0 when apt ran
# (the caller re-verifies presence); 1 when no usable apt exists.
_APT_SUDO=""
_APT_INIT_DONE=""
_apt_init() {
    [[ -n "${_APT_INIT_DONE}" ]] && return 0
    _APT_INIT_DONE=1
    if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
        _APT_SUDO=""
    elif _have_cmd sudo && sudo -n true 2>/dev/null; then
        _APT_SUDO="sudo -n"
    else
        _APT_SUDO="__NONE__"
    fi
    return 0
}
_apt_install_pkgs() {
    local _pkgs=("$@") _p _f
    _apt_init
    if [[ "${_APT_SUDO}" == "__NONE__" ]] || ! _have_cmd apt-get; then
        echo "ERROR: cannot install packages — no usable apt (need root or passwordless sudo)."
        echo "  Run this by hand, then re-run the build:"
        echo "    sudo apt-get install -y ${_pkgs[*]}"
        return 1
    fi
    if [[ -z "${CPGE_APT_UPDATED:-}" ]]; then
        # No update is needed when package lists already exist: apt
        # writes *_Packages into place only when complete, so existing
        # lists are usable even if they are a day old (and an update can
        # take ten minutes on a slow mirror).
        if ls /var/lib/apt/lists/*_Packages >/dev/null 2>&1; then
            export CPGE_APT_UPDATED=1
        else
            echo "Dependency check: refreshing package lists (apt-get update — can take a while on a slow mirror)..."
            if ${_APT_SUDO} apt-get update; then
                export CPGE_APT_UPDATED=1
            else
                echo "NOTE: apt-get update failed — continuing with the cached package lists."
            fi
        fi
    fi
    echo "Dependency check: installing missing packages: ${_pkgs[*]}"
    if ${_APT_SUDO} apt-get install -y "${_pkgs[@]}"; then
        return 0
    fi
    echo "NOTE: apt-get install failed — trying the cached .debs with dpkg..."
    for _p in "${_pkgs[@]}"; do
        for _f in /var/cache/apt/archives/"${_p}"_*.deb; do
            [[ -f "$_f" ]] || continue
            ${_APT_SUDO} dpkg -i "$_f" 2>/dev/null || true
        done
    done
    return 0
}

# --- JDK 17 (hard) ----------------------------------------------------------
# Accepts JAVA_HOME first, then any jdk-17* dir in the SDK tree (the
# Temurin install, jdk-17.0.21.1+2), then javac on PATH (version 17+).
# Sets JDK_DIR on success. When none is found the Temurin JDK 17 is
# downloaded (Adoptium API, latest 17 GA) and installed to
# $SDK_ROOT/jdk-17.0.21.1+2 — the exact name Android/package-apk.sh
# looks for — and JAVA_HOME is exported so the packaging step finds it.
_jdk_present() {
    local _j _v
    if [[ -n "${JAVA_HOME:-}" ]]; then
        _j="$(posix_path "$JAVA_HOME")"
        if [[ -x "$_j/bin/javac" ]]; then JDK_DIR="$_j"; return 0; fi
    fi
    for _j in "${SDK_ROOT}"/jdk-17*/; do
        [[ -x "${_j}bin/javac" ]] && { JDK_DIR="${_j%/}"; return 0; }
    done
    if _have_cmd javac; then
        _v="$(javac -version 2>&1 | grep -oE '[0-9]+' | head -1)"
        if [[ "${_v:-0}" -ge 17 ]]; then
            JDK_DIR="$(dirname "$(dirname "$(command -v javac)")")"
            return 0
        fi
    fi
    return 1
}
_download_jdk() {
    local _os _url _tmp _file
    _os="$(_android_os)"
    _url="https://api.adoptium.net/v3/binary/latest/17/ga/${_os}/x64/jdk/hotspot/normal/eclipse"
    _tmp="$(mktemp -d "${SDK_ROOT}/.cpge-dl-XXXXXX")" || return 1
    if [[ "$_os" == "windows" ]]; then _file="$_tmp/jdk17.zip"; else _file="$_tmp/jdk17.tar.gz"; fi
    if ! _download "$_url" "$_file"; then rm -rf "$_tmp"; return 1; fi
    if ! _install_archive "$_file" "${SDK_ROOT}/jdk-17.0.21.1+2"; then rm -rf "$_tmp"; return 1; fi
    rm -rf "$_tmp"
    export JAVA_HOME="${SDK_ROOT}/jdk-17.0.21.1+2"
    return 0
}

# --- SDK pieces ---------------------------------------------------------------
# _sdk_download_and_install <url> <destdir> — download + extract into
# destdir, then return 0 only if destdir is non-empty.
_sdk_download_and_install() {
    local _url="$1" _dest="$2" _tmp _file
    _tmp="$(mktemp -d "${SDK_ROOT}/.cpge-dl-XXXXXX")" || return 1
    _file="$_tmp/$(basename "$_url")"
    if ! _download "$_url" "$_file"; then rm -rf "$_tmp"; return 1; fi
    if ! _install_archive "$_file" "$_dest"; then rm -rf "$_tmp"; return 1; fi
    rm -rf "$_tmp"
    [[ -n "$(ls -A "$_dest" 2>/dev/null)" ]]
}

# _newest_build_tools — the newest installed build-tools dir holding
# aapt2 (via find_tool, so .exe/.bat on Windows resolve). Prints the
# dir or nothing.
_newest_build_tools() {
    local _bt _a
    for _bt in "${SDK_ROOT}"/build-tools/*/; do
        [[ -d "$_bt" ]] || continue
        _a="$(find_tool "$_bt" aapt2 || true)"
        [[ -n "$_a" ]] && printf '%s' "${_bt%/}"
    done | sort -V | tail -1
}

# _build_tools_complete <dir> — aapt2, zipalign, apksigner and d8 all
# resolve in the dir (via find_tool, so .exe/.bat on Windows resolve).
_build_tools_complete() {
    local _d="$1" _t
    for _t in aapt2 zipalign apksigner d8; do
        [[ -n "$(find_tool "$_d" "$_t" || true)" ]] || return 1
    done
    return 0
}

# --- Archiver pre-flight ------------------------------------------------------
# Same resolution order as Android/package-apk.sh (CPGE_7Z, zip, 7z,
# 7za, 7-Zip registry, standard C:/D: locations, wzzip on PATH, wzzip
# App Paths, wzzip standard locations). Sets _A_TOOL/_A_CMD; returns 0
# when one resolves. Only a pre-flight — package-apk.sh resolves the
# archiver itself at packaging time; this fails early, before the
# build, instead of after it.
_A_TOOL=""; _A_CMD=""
_resolve_7z_in_dir() {
    local _z
    _z="$(find_tool "$1" 7z || true)"
    if [[ -n "$_z" ]]; then _A_TOOL=7z; _A_CMD="$_z"; return 0; fi
    _z="$(find_tool "$1" 7za || true)"
    if [[ -n "$_z" ]]; then _A_TOOL=7za; _A_CMD="$_z"; return 0; fi
    return 1
}
_resolve_archiver() {
    local _d _wz
    _A_TOOL=""; _A_CMD=""
    if [[ -n "${CPGE_7Z:-}" ]]; then
        if [[ -f "$CPGE_7Z" ]]; then
            case "$(basename "$CPGE_7Z")" in
                7za|7za.*) _A_TOOL=7za ;;
                *) _A_TOOL=7z ;;
            esac
            _A_CMD="$CPGE_7Z"
        elif [[ -d "$CPGE_7Z" ]]; then
            _resolve_7z_in_dir "$CPGE_7Z" || true
        fi
        if [[ -z "$_A_TOOL" ]]; then
            echo "NOTE: CPGE_7Z is set ($CPGE_7Z) but no 7z/7za executable was found there — ignoring it."
        fi
    fi
    if [[ -z "$_A_TOOL" ]] && _have_cmd zip; then _A_TOOL=zip; _A_CMD="$(command -v zip)"; fi
    if [[ -z "$_A_TOOL" ]] && _have_cmd 7z; then _A_TOOL=7z; _A_CMD="$(command -v 7z)"; fi
    if [[ -z "$_A_TOOL" ]] && _have_cmd 7za; then _A_TOOL=7za; _A_CMD="$(command -v 7za)"; fi
    if [[ -z "$_A_TOOL" ]] && _have_cmd reg; then
        local _key _reg_path _reg_dir
        for _key in "HKLM\\SOFTWARE\\7-Zip" "HKLM\\SOFTWARE\\WOW6432Node\\7-Zip"; do
            _reg_path="$(reg query "$_key" /v Path 2>/dev/null | awk -F'REG_SZ' 'NF>1 {gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2; exit}' || true)"
            if [[ -n "$_reg_path" ]]; then
                _reg_dir="$(posix_path "$_reg_path")"
                _reg_dir="${_reg_dir%/}"
                if _resolve_7z_in_dir "$_reg_dir"; then break; fi
            fi
        done
    fi
    if [[ -z "$_A_TOOL" ]]; then
        for _d in "/c/Program Files/7-Zip" "/c/Program Files (x86)/7-Zip" "/d/Program Files/7-Zip" "/d/Program Files (x86)/7-Zip"; do
            if _resolve_7z_in_dir "$_d"; then break; fi
        done
    fi
    if [[ -z "$_A_TOOL" ]] && _have_cmd wzzip; then _A_TOOL=wzzip; _A_CMD="$(command -v wzzip)"; fi
    if [[ -z "$_A_TOOL" ]] && _have_cmd reg; then
        local _wz_path _wz_posix
        _wz_path="$(reg query "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\wzzip.exe" /ve 2>/dev/null | awk -F'REG_SZ' 'NF>1 {gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2; exit}' || true)"
        if [[ -n "$_wz_path" ]]; then
            _wz_posix="$(posix_path "$_wz_path")"
            if [[ -f "$_wz_posix" ]]; then _A_TOOL=wzzip; _A_CMD="$_wz_posix"; fi
        fi
    fi
    if [[ -z "$_A_TOOL" ]]; then
        for _d in "/c/Program Files/WinZip" "/c/Program Files (x86)/WinZip"; do
            _wz="$(find_tool "$_d" wzzip || true)"
            if [[ -n "$_wz" ]]; then _A_TOOL=wzzip; _A_CMD="$_wz"; break; fi
        done
    fi
    [[ -n "$_A_TOOL" ]]
}

# --- NDK resolution (moved here from the old inline block) --------------------
# Sets NDK_ROOT from ANDROID_NDK / ANDROID_NDK_HOME / ANDROID_HOME /
# the newest ndk/<ver> dir. Returns 1 when no usable NDK is found.
resolve_ndk() {
    NDK_ROOT=""
    local _candidate
    for _candidate in \
            "${ANDROID_NDK:-}" \
            "${ANDROID_NDK_HOME:-}" \
            "${ANDROID_HOME:-}/ndk-bundle" \
            "$(newest_ndk_dir "${ANDROID_HOME:-/nonexistent}/ndk")" \
            "$(newest_ndk_dir "${SDK_ROOT}/ndk")"; do
        if [[ -n "${_candidate}" && -d "${_candidate}" && -f "${_candidate}/build/cmake/android.toolchain.cmake" ]]; then
            NDK_ROOT="${_candidate}"
            return 0
        fi
    done
    return 1
}

# _download_ndk — NDK r27c (27.2.12479018, the default) from
# dl.google.com, installed to $SDK_ROOT/ndk/27.2.12479018.
_download_ndk() {
    local _os _url _tmp _file
    _os="$(_android_os)"
    _url="https://dl.google.com/android/repository/android-ndk-r27c-${_os}.zip"
    _tmp="$(mktemp -d "${SDK_ROOT}/.cpge-dl-XXXXXX")" || return 1
    _file="$_tmp/android-ndk-r27c-${_os}.zip"
    if ! _download "$_url" "$_file"; then rm -rf "$_tmp"; return 1; fi
    if ! _install_archive "$_file" "${SDK_ROOT}/ndk/27.2.12479018"; then rm -rf "$_tmp"; return 1; fi
    rm -rf "$_tmp"
    return 0
}

# The Android dependency check. SDK_ROOT must be set before calling.
# Returns 1 when a HARD dependency is missing and could not be
# downloaded; soft failures only warn. Sets NDK_ROOT and JDK_DIR.
check_android_deps() {
    local _os _bt _pt
    _os="$(_android_os)"

    # --- Tools (hard): cmake, make, curl ---
    if ! _have_cmd "${CMAKE_EXE}"; then
        if _is_linux; then
            echo "Dependency check: cmake missing — installing..."
            _apt_install_pkgs cmake || true
        fi
        if ! _have_cmd "${CMAKE_EXE}"; then
            echo "ERROR: cmake not found."
            echo "  Install it via Android Studio's bundled cmake, or:"
            echo "    sdkmanager 'cmake;<version>'"
            echo "  Or on Linux: sudo apt-get install -y cmake"
            echo "  Or set CMAKE_EXE to the full path of your cmake binary."
            return 1
        fi
    fi
    if ! _have_cmd make; then
        if _is_linux; then
            echo "Dependency check: make missing — installing..."
            _apt_install_pkgs make || true
        fi
        if ! _have_cmd make; then
            echo "ERROR: make not found — the Android build uses Make (the NDK does not bundle ninja)."
            echo "  On Linux: sudo apt-get install -y make"
            echo "  On Windows: Git for Windows provides make in Git Bash (usr/bin/make)."
            return 1
        fi
    fi
    if ! _have_cmd curl; then
        echo "ERROR: curl not found — it is required to download missing dependencies."
        echo "  On Linux: sudo apt-get install -y curl"
        echo "  On Windows: Git for Windows provides curl in Git Bash."
        return 1
    fi

    # --- JDK 17 (hard) ---
    if ! _jdk_present; then
        echo "Dependency check: JDK 17 missing — downloading Temurin 17..."
        if ! _download_jdk || ! _jdk_present; then
            echo "ERROR: no JDK 17 found and the download failed."
            echo "  Download Temurin 17 from https://adoptium.net/temurin/releases/?version=17"
            echo "  and set JAVA_HOME to it, e.g.:"
            echo "    export JAVA_HOME=/path/to/jdk-17"
            return 1
        fi
        echo "Dependency check: JDK 17 installed: ${JDK_DIR}"
    fi

    # --- SDK: cmdline tools (hard) ---
    if ! _have_file "${SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager" && ! _have_file "${SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager.bat"; then
        echo "Dependency check: Android cmdline tools missing — downloading..."
        if ! _sdk_download_and_install "https://dl.google.com/android/repository/commandlinetools-${_os}-11076708_latest.zip" "${SDK_ROOT}/cmdline-tools/latest"; then
            echo "ERROR: could not download the Android cmdline tools."
            echo "  Install them by hand (Android Studio → SDK Manager → SDK Tools → Android SDK Command-line Tools)"
            echo "  or download https://dl.google.com/android/repository/commandlinetools-${_os}-11076708_latest.zip"
            echo "  and extract it to ${SDK_ROOT}/cmdline-tools/latest"
            return 1
        fi
        echo "Dependency check: cmdline tools installed."
    fi

    # --- SDK: platform-tools / adb (soft — only needed to install on a device) ---
    if ! _have_file "${SDK_ROOT}/platform-tools/adb" && ! _have_file "${SDK_ROOT}/platform-tools/adb.exe"; then
        echo "Dependency check: platform-tools (adb) missing — downloading..."
        if ! _sdk_download_and_install "https://dl.google.com/android/repository/platform-tools-latest-${_os}.zip" "${SDK_ROOT}/platform-tools"; then
            echo "WARNING: platform-tools (adb) could not be downloaded — device install will need adb by hand."
            echo "  Download https://dl.google.com/android/repository/platform-tools-latest-${_os}.zip"
            echo "  and extract it to ${SDK_ROOT}/platform-tools"
        else
            echo "Dependency check: platform-tools installed."
        fi
    fi

    # --- SDK: build-tools (hard) — need aapt2, zipalign, apksigner, d8 ---
    _bt="$(_newest_build_tools)"
    if [[ -z "$_bt" ]] || ! _build_tools_complete "$_bt"; then
        echo "Dependency check: build-tools (aapt2/zipalign/apksigner/d8) missing — downloading build-tools r34..."
        if ! _sdk_download_and_install "https://dl.google.com/android/repository/build-tools_r34-${_os}.zip" "${SDK_ROOT}/build-tools/34.0.0"; then
            echo "ERROR: could not download build-tools r34."
            echo "  Install them by hand (Android Studio → SDK Manager → SDK Tools → Android SDK Build-Tools 34.0.0)"
            echo "  or download https://dl.google.com/android/repository/build-tools_r34-${_os}.zip"
            echo "  and extract it to ${SDK_ROOT}/build-tools/34.0.0"
            return 1
        fi
        _bt="$(_newest_build_tools)"
        if [[ -z "$_bt" ]] || ! _build_tools_complete "$_bt"; then
            echo "ERROR: build-tools r34 downloaded but aapt2/zipalign/apksigner/d8 still do not resolve in ${SDK_ROOT}/build-tools/34.0.0"
            return 1
        fi
        echo "Dependency check: build-tools 34.0.0 installed."
    fi

    # --- SDK: platform android-XX (hard) — need android.jar for aapt2 link ---
    _pt=""
    for _pt in "${SDK_ROOT}"/platforms/android-*/android.jar; do
        [[ -f "$_pt" ]] && break
    done
    if [[ ! -f "${_pt:-}" ]]; then
        echo "Dependency check: no Android platform (android.jar) installed — downloading platform android-34..."
        if ! _sdk_download_and_install "https://dl.google.com/android/repository/platform-34-ext12_r01.zip" "${SDK_ROOT}/platforms/android-34"; then
            echo "ERROR: could not download platform android-34."
            echo "  Install it by hand (Android Studio → SDK Manager → SDK Platforms → Android 14.0, API 34)"
            echo "  or download https://dl.google.com/android/repository/platform-34-ext12_r01.zip"
            echo "  and extract it to ${SDK_ROOT}/platforms/android-34"
            return 1
        fi
        [[ -f "${SDK_ROOT}/platforms/android-34/android.jar" ]] || {
            echo "ERROR: platform android-34 downloaded but android.jar is not in ${SDK_ROOT}/platforms/android-34"
            return 1
        }
        echo "Dependency check: platform android-34 installed."
    fi

    # --- NDK (hard) — resolve, else download r27c, else fail ---
    if ! resolve_ndk; then
        echo "Dependency check: Android NDK not found — downloading NDK r27c (27.2.12479018, ~664 MB)..."
        if ! _download_ndk || ! resolve_ndk; then
            echo "ERROR: Android NDK not found and the download failed."
            echo ""
            echo "Set one of the following environment variables to the NDK root directory:"
            echo "  export ANDROID_NDK=/path/to/ndk/<version>"
            echo "  export ANDROID_NDK_HOME=/path/to/ndk/<version>"
            echo "  export ANDROID_HOME=/path/to/sdk   (ndk-bundle/ or ndk/<ver> must exist)"
            echo ""
            echo "Install the NDK via Android Studio:"
            echo "  SDK Manager → SDK Tools → NDK (Side by side)"
            echo "Or download it by hand:"
            echo "  https://dl.google.com/android/repository/android-ndk-r27c-${_os}.zip"
            echo "  and extract it to ${SDK_ROOT}/ndk/27.2.12479018"
            return 1
        fi
        echo "Dependency check: NDK r27c installed."
    fi
    echo "Android NDK : ${NDK_ROOT}"

    # --- Archiver pre-flight (hard) — same order as package-apk.sh ---
    if ! _resolve_archiver; then
        echo "ERROR: no archiver found — needed to stage the APK contents (zip, 7z, 7za or wzzip)."
        echo "  7-Zip does NOT provide a \`zip\` command — its command is \`7z\`. Installing"
        echo "  7-Zip alone never satisfies a \`zip\` check. Either:"
        echo "    - get \`zip\` (on Git Bash: pacman -S zip), or"
        echo "    - install 7-Zip and make sure \`7z\` is on PATH (add C:\\Program Files\\7-Zip"
        echo "      to PATH), or use \`7za\` — NCH ExpressZip bundles 7za.exe (the"
        echo "      standalone 7-Zip console, same commands as 7z) and the script"
        echo "      accepts it, or use \`wzzip\` — WinZip's command-line tool, from the"
        echo "      free WinZip Command Line Support Add-On (separate download from"
        echo "      winzip.com; it is NOT in base WinZip). A PATH change only applies"
        echo "      to a NEW Git Bash window — close and reopen Git Bash after editing"
        echo "      PATH, or the old shell still has the old PATH. Or point the script"
        echo "      straight at your 7z.exe or 7za.exe:"
        echo "        CPGE_7Z='/d/Tools/7-Zip/7z.exe'   (path to the 7z/7za executable, any location)"
        return 1
    fi

    # --- glslangValidator (soft, Linux only) ---
    if _is_linux && ! _have_cmd glslangValidator; then
        echo "Dependency check: glslangValidator missing (optional) — installing..."
        _apt_install_pkgs glslang-tools || true
        _have_cmd glslangValidator || echo "WARNING: glslangValidator still missing — Vulkan shaders are copied for runtime shaderc compile instead. Fix: sudo apt-get install -y glslang-tools"
    fi
    return 0
}

# --- Install the APK on a connected device (25 November 2026) ---------------
# After the APK is packaged, the build offers to install it on a device
# the user picks — USB hardware or an emulator, both appear in
# `adb devices` (Daniel, 25 Nov 2026: "we need to have the ability to
# give the apk and install it onto a selectable device it knows; this
# includes via USB connectivity ... both hardware and emulated
# systems"). The prompt only runs when stdin is a terminal; a
# non-interactive run (CI, a pipe, a redirect) skips it with a one-line
# note naming the manual command. The install never fails the build: a
# decline, a skip, a missing adb or a failed install is reported and
# the build's status stands.
#
# Flags (parsed with the other switches at the top of the script):
# -install installs without prompting when a device is present
# (exactly one device → it; several → the first listed, and the log
# says which); -no-install skips the prompt entirely. -no-install wins
# when both are given.
#
# adb is resolved from the SDK platform-tools dir first (via find_tool,
# so adb.exe on Windows resolves), then from PATH. CPGE_ADB overrides
# both (a path to an adb binary — the escape hatch, same pattern as
# CPGE_7Z for the archiver). The dependency check downloads
# platform-tools when it is missing (soft — it only warns), so a
# missing adb here just means "install by hand".
resolve_adb() {  # prints the adb path or nothing
    local _adb
    if [[ -n "${CPGE_ADB:-}" ]]; then
        if [[ -x "$CPGE_ADB" ]]; then printf '%s' "$CPGE_ADB"; return 0; fi
        echo "NOTE: CPGE_ADB is set ($CPGE_ADB) but is not an executable — ignoring it."
    fi
    _adb="$(find_tool "${SDK_ROOT}/platform-tools" adb || true)"
    if [[ -n "$_adb" ]]; then printf '%s' "$_adb"; return 0; fi
    if command -v adb &>/dev/null; then command -v adb; return 0; fi
    return 1
}

# _adb_devices <adb> — prints one "serial<TAB>state<TAB>model" line per
# connected device in state "device" (offline/unauthorised devices are
# listed by adb but cannot take an install, so they are excluded).
# `adb devices -l` gives the model in the model: field. Windows adb
# output ends lines with CRLF, so \r is stripped before parsing. The
# listing is guarded with `timeout` when it exists (20 s) so a wedged
# adb server can never hang the build; without `timeout` the plain
# `adb devices` call is used (it is a fast local query).
_adb_devices() {
    local _adb="$1" _out
    if command -v timeout &>/dev/null; then
        _out="$(timeout 20 "$_adb" devices -l 2>/dev/null || true)"
    else
        _out="$("$_adb" devices -l 2>/dev/null || true)"
    fi
    printf '%s' "$_out" | tr -d '\r' | awk '
        $2 == "device" {
            model = ""
            for (i = 3; i <= NF; i++) if ($i ~ /^model:/) { sub(/^model:/, "", $i); model = $i }
            print $1 "\t" $2 "\t" model
        }'
}

maybe_install_apk() {  # maybe_install_apk <apk-path>
    local _apk="$1" _adb _list _count _i _serial _state _model _answer _picked
    local -a _serials=() _models=()

    # -no-install: skip entirely.
    if [[ -n "${NO_INSTALL:-}" ]]; then
        echo "NOTE: device install skipped (-no-install)."
        return 0
    fi

    # The APK must exist — it was just packaged, but be defensive.
    if [[ ! -f "$_apk" ]]; then
        echo "NOTE: APK not found at $_apk — skipping device install."
        return 0
    fi

    # adb resolution.
    _adb="$(resolve_adb || true)"
    if [[ -z "$_adb" ]]; then
        echo "NOTE: adb not found — cannot install on a device. adb is part of platform-tools; install it with: sdkmanager \"platform-tools\", or install by hand with: adb install -r ${_apk}"
        return 0
    fi

    # List usable devices.
    _list="$(_adb_devices "$_adb")"
    if [[ -z "$_list" ]]; then
        echo "NOTE: no usable devices found (adb devices shows none in state 'device') — install manually with: adb install -r ${_apk}"
        return 0
    fi
    while IFS=$'\t' read -r _serial _state _model; do
        _serials+=("$_serial")
        _models+=("$_model")
    done <<< "$_list"
    _count="${#_serials[@]}"

    # -install: no prompt. Exactly one device → it; several → the first
    # listed (the log says which).
    if [[ -n "${INSTALL_APK:-}" ]]; then
        _picked=0
        if [[ "$_count" -gt 1 ]]; then
            echo "NOTE: -install with $_count devices — installing on the first: ${_serials[0]} (${_models[0]:-unknown model})."
        fi
    else
        # Interactive prompt — only when stdin is a terminal.
        if [[ ! -t 0 ]]; then
            echo "NOTE: not a terminal — skipping device install. To install by hand: adb install -r ${_apk}"
            return 0
        fi
        echo "Devices:"
        _i=0
        while [[ "$_i" -lt "$_count" ]]; do
            echo "  [$((_i + 1))] ${_serials[$_i]} (${_models[$_i]:-unknown model}, device)"
            _i=$((_i + 1))
        done
        # read -t so the prompt can never hang the build: 60 s to
        # answer, then it is a decline. (stdin is a terminal here —
        # checked above — so read will not return at once on EOF.)
        if ! read -t 60 -r -p "Install ${_apk} on a device? [1-${_count}, n to skip] " _answer; then
            echo "NOTE: no answer — skipping device install. To install by hand: adb install -r ${_apk}"
            return 0
        fi
        case "${_answer,,}" in
            n|no|"") echo "NOTE: device install declined — to install by hand: adb install -r ${_apk}"; return 0 ;;
        esac
        if ! [[ "$_answer" =~ ^[0-9]+$ ]] || (( _answer < 1 || _answer > _count )); then
            echo "NOTE: '${_answer}' is not a device number — skipping device install. To install by hand: adb install -r ${_apk}"
            return 0
        fi
        _picked=$((_answer - 1))
    fi

    _serial="${_serials[$_picked]}"
    _model="${_models[$_picked]}"
    echo "Installing ${_apk} on ${_serial} (${_model:-unknown model})..."
    # -r so a reinstall keeps the app's data; it works for a fresh
    # install too. A failed install is a warning, never a build failure.
    if "$_adb" -s "$_serial" install -r "$_apk"; then
        echo "Installed on ${_serial}."
    else
        echo "WARNING: adb install failed on ${_serial} — the APK is at ${_apk}; install by hand with: adb -s ${_serial} install -r ${_apk}"
    fi
    return 0
}

# --- Reset the on-device scores + recordings (25 November 2026) ---------
# Best effort only, and it never fails the build: a fresh install starts
# clean, and `adb install -r` preserves internal storage, so a clean
# release build also clears the on-device table when a device is
# connected (every connected device in state "device" — usually one).
# The build-machine reset (Linux/reset.sh) is the hard-fail step; this
# one only ever prints a note.
#
# Paths: the engine's writable home on Android is internal storage
# (AndroidApp::GetInternalDataPath, /data/data/<pkg>/files) and its
# runtime Assets/ is baseDir + "/Assets" (main.cpp), so the table is
# <files>/Assets/highscores.txt with recordings in
# <files>/Assets/Playbacks/. The <files>/ root copies are cleared too:
# the SetFile binding has no caller in the current code, so both
# locations are plausible and both live inside the app's own storage.
#
# Access: /data/data/<pkg> is not readable by the adb shell user, so
# this uses `run-as <pkg>` — which only works for debuggable apps
# (debug builds). A release build is not debuggable, so on a release
# install the device step notes the skip and the build still succeeds;
# a debug build on a connected device is cleared properly. If the game
# is running on the device, close it first — it would rewrite the
# table when it exits.
reset_android_device() {  # reset_android_device — always returns 0
    local _adb _list _serial _state _model _pkg _files
    _pkg="${CPGE_ANDROID_PACKAGE:-com.ultimanium.cpge}"
    _files="/data/data/${_pkg}/files"

    _adb="$(resolve_adb || true)"
    if [[ -z "$_adb" ]]; then
        echo "NOTE: on-device reset skipped — adb not found (install platform-tools to clear scores on a device: sdkmanager \"platform-tools\")."
        return 0
    fi
    _list="$(_adb_devices "$_adb")"
    if [[ -z "$_list" ]]; then
        echo "NOTE: on-device reset skipped — no device connected."
        return 0
    fi
    while IFS=$'\t' read -r _serial _state _model; do
        [[ -z "$_serial" ]] && continue
        # run-as runs as the app user, so the app's own files/ is
        # reachable; absolute paths are used so the call does not
        # depend on run-as's working directory. The table file goes
        # with plain rm; the recordings need a shell glob inside
        # run-as (adb does not expand wildcards for the remote side).
        if "$_adb" -s "$_serial" shell run-as "$_pkg" rm -f "${_files}/Assets/highscores.txt" "${_files}/highscores.txt" 2>/dev/null \
            && "$_adb" -s "$_serial" shell run-as "$_pkg" sh -c "rm -f ${_files}/Assets/Playbacks/*.mp4 ${_files}/Assets/Playbacks/*.json ${_files}/Assets/Playbacks/*.cpr ${_files}/Playbacks/*.mp4 ${_files}/Playbacks/*.json ${_files}/Playbacks/*.cpr" 2>/dev/null; then
            echo "On-device reset: cleared high scores and recordings on ${_serial} (${_model:-unknown model})."
        else
            echo "NOTE: on-device reset skipped on ${_serial} — run-as failed (release builds are not debuggable, so their internal storage cannot be cleared over adb; uninstall the app first, or use a debug build)."
        fi
    done <<< "$_list"
    return 0
}

# --- Install to root + Gradle project, then package the APK -----------------
# Runs on success only. Automates the three "Next steps" this script used
# to print, in order, and then builds the APK AFTER the install so the APK
# packages the root .so and Assets/:
#   0. Clean release builds only: reset the high scores and their
#      recordings for a fresh start (Linux/reset.sh) BEFORE the Assets/
#      sync at step 2, so the APK ships a fresh table; the on-device
#      table is cleared too when a device is connected (best effort —
#      see reset_android_device above, never fails the build). A failed
#      build-machine reset stops the build here, before packaging.
#   1. libCPGE.so to the project root and to jniLibs/<abi>/.
#   2. Assets/ to the Gradle project's assets/ (AIHistory excluded).
#   3. NativeActivity entry in AndroidManifest.xml (verified, repaired
#      if missing or wrong — see ensure_native_activity above).
#   4. classes.dex via Android/build-dex.sh, then the APK via
#      Android/package-apk.sh (aapt2), debug and release, arm64-v8a
#      only; skipped with a clear note for another ABI. A failed dex
#      build stops the build here, before packaging. Release builds
#      are stripped (llvm-strip) before packaging.
#   5. Offer to install the APK on a connected device (adb — see
#      maybe_install_apk above). Interactive sessions only; never
#      fails the build.
install_and_package_android() {
    local _so="${BUILD_DIR}/libCPGE.so"
    if [[ ! -f "${_so}" ]]; then
        echo "ERROR: expected library not found after a successful build: ${_so}"
        return 1
    fi

    # 0. Clean release builds only: reset the high scores and their
    #    recordings for a fresh start (Daniel, 25 Nov 2026). Runs
    #    BEFORE the Assets/ sync at step 2, so the staged Assets/ —
    #    and the APK packaged from it — carry a fresh table. The
    #    build-machine reset is a hard failure (a release must never
    #    ship with stale scores); the on-device reset is best effort
    #    and never fails the build (see reset_android_device).
    if [[ "${CONFIG}" == "Release" && -n "${CLEAN_BUILD}" ]]; then
        echo "Resetting high scores and recordings for a fresh start (clean release build)..."
        bash "${SCRIPT_DIR}/reset.sh" || { echo "ERROR: reset failed (Linux/reset.sh) — the build is stopped so a clean release never ships with stale scores."; return 1; }
        reset_android_device || true
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

    # 4. classes.dex (the Java TTS JNI helper) and the APK, built AFTER
    #    the install so the APK packages the .so, Assets/ and the dex.
    #    Debug and release both package (arm64-v8a only); the config
    #    is passed through so package-apk.sh strips the release .so
    #    and leaves the debug one alone.
    local _apk_script="${PROJECT_ROOT}/Android/package-apk.sh"
    if [[ "${ABI}" == "arm64-v8a" ]]; then
        if [[ -x "${_apk_script}" ]]; then
            # 4a. Build classes.dex first. The APK must carry it — the
            #     manifest has android:hasCode=true — so a failed dex
            #     build stops the build here, before packaging, instead
            #     of producing an APK without TTS. The packaging
            #     scripts resolve the SDK/JDK from the same roots this
            #     script did, so pass them on (ANDROID_HOME for the
            #     SDK root, JAVA_HOME for the JDK the dependency check
            #     found) rather than letting each script re-derive
            #     them from defaults.
            export ANDROID_HOME="${SDK_ROOT}"
            if [[ -n "${JDK_DIR:-}" ]]; then export JAVA_HOME="${JDK_DIR}"; fi
            local _dex_script="${PROJECT_ROOT}/Android/build-dex.sh"
            if [[ -x "${_dex_script}" ]]; then
                echo "Building classes.dex (TTS JNI helper): ${_dex_script}"
                if "${_dex_script}"; then
                    echo "classes.dex built: ${PROJECT_ROOT}/Android/classes.dex"
                else
                    echo "ERROR: classes.dex build failed (Android/build-dex.sh)."
                    echo "  The APK cannot be packaged without the TTS JNI helper."
                    echo "  Library build: SUCCEEDED (libCPGE.so installed to the project root and jniLibs/${ABI}/)."
                    return 1
                fi
            else
                # The dex build script ships in the repo — its absence
                # means a broken checkout, so fail loudly.
                echo "ERROR: dex build script missing: ${_dex_script}"
                echo "  It ships in the repo at Android/build-dex.sh — your checkout is incomplete."
                echo "  Library build: SUCCEEDED (libCPGE.so installed to the project root and jniLibs/${ABI}/)."
                return 1
            fi
            echo "Packaging APK with aapt2: ${_apk_script} ${ARG1,,} ${CONFIG,,}"
            if "${_apk_script}" "${ARG1,,}" "${CONFIG,,}"; then
                echo "APK packaged: Android/cpge-${ARG1,,}-${CONFIG,,}.apk"
                # 5. Offer to install the APK on a connected device
                #    (interactive sessions only — see
                #    maybe_install_apk). Never fails the build.
                maybe_install_apk "${PROJECT_ROOT}/Android/cpge-${ARG1,,}-${CONFIG,,}.apk" || true
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
    echo "  -install / -no-install : install the APK on a device after packaging (prompt, or skip)"
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

# --- Locate Android SDK root + dependency check ---------------------------
# Newest NDK install below a given SDK ndk/ directory. Directories only:
# a stray file (e.g. a downloaded NDK .zip) must not be picked up as the
# "newest NDK" — that is what happened when android-ndk-r30-linux.zip sat
# next to the NDK installs and sort -V picked the .zip over the dirs.
newest_ndk_dir() {
    local _base="$1"
    ls -d "${_base}"/*/ 2>/dev/null | sort -V | tail -1
}

# SDK root: ANDROID_HOME first, then ANDROID_SDK_ROOT, then the default
# for the host — ~/workspace/android-sdk on Linux (the Phase D install
# location, so the script works on the build machine without any env
# vars; source ~/workspace/android-sdk/env.sh for the explicit form),
# %LOCALAPPDATA%\Android\Sdk on Windows (the standard location). The
# dependency check creates the default root and downloads the SDK
# pieces into it when nothing is found.
if [[ -n "${ANDROID_HOME:-}" ]]; then
    SDK_ROOT="$(posix_path "$ANDROID_HOME")"
elif [[ -n "${ANDROID_SDK_ROOT:-}" ]]; then
    SDK_ROOT="$(posix_path "$ANDROID_SDK_ROOT")"
elif _is_windows; then
    SDK_ROOT="$(posix_path "${LOCALAPPDATA:-$HOME/AppData/Local}/Android/Sdk")"
else
    SDK_ROOT="$HOME/workspace/android-sdk"
fi
mkdir -p "${SDK_ROOT}" || { echo "ERROR: could not create the Android SDK root: ${SDK_ROOT}"; exit 1; }

# --- Dependency check (before building, for debug AND release) -----------
# Verifies every dependency the build needs and downloads any that are
# missing (see check_android_deps above). A missing required dependency
# that cannot be downloaded stops the build here, before CMake runs.
check_android_deps || exit 1

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
