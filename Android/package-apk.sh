#!/bin/bash
# package-apk.sh — manual aapt2 APK packaging for CPGE Android (Phase D step 3, 24 Nov 2026; moved into the repo at Android/ on 25 Nov 2026).
#
# Gradle cannot run in this VM (its client<->daemon loopback TCP is
# intercepted by the sandbox network policy), and Daniel has decided
# (24 Nov 2026) that manual aapt2 packaging is the PRIMARY path and
# Gradle is the fallback only — do not wait on the other_tcp setting.
# This script is therefore how APKs are produced, not a stopgap.
#
# Usage: ./package-apk.sh <opengl|vulkan> [debug|release]   (run from Android/
#        or anywhere; all paths are resolved from this script's location)
#        The config defaults to debug, so existing callers are unaffected.
# Output: Android/cpge-<renderer>-<config>.apk (signed, zipaligned).
#        Debug keeps cpge-<renderer>-debug.apk (debuggable, debug-signed,
#        unstripped). Release makes cpge-<renderer>-release.apk: the .so is
#        stripped of debug info with llvm-strip (from the NDK), aapt2 link
#        runs WITHOUT --debug-mode (so the APK is not debuggable), and it
#        is signed with the release keystore when CPGE_RELEASE_KEYSTORE is
#        set (with CPGE_RELEASE_KEYSTORE_PASS, CPGE_RELEASE_KEY_ALIAS and
#        CPGE_RELEASE_KEY_PASS); otherwise it is signed with the debug
#        keystore and the script says so — set a release keystore before
#        distributing a release APK. The .so is taken from the build dir
#        for the requested config first
#        (Linux/build-android/arm64-v8a/<Renderer>/<Config>/libCPGE.so —
#        the exact artifact for that config), falling back to the
#        repo-root copy (which is only correct when it matches the
#        requested config).
# The Java TTS helper (CpgeTts) is compiled to classes.dex by
# build-dex.sh (same folder) and added automatically when it exists; the
# manifest's android:hasCode is true, so the dex must be present for the
# TTS class to load (before step 3 the APK shipped without a dex).
#
# NDK (release only): resolved the same way Linux/cmake-build-android.sh
# does — ANDROID_NDK, then ANDROID_NDK_HOME, then the newest dir under
# ANDROID_HOME/ndk, then ~/workspace/android-sdk/ndk. llvm-strip comes
# from $NDK/toolchains/llvm/prebuilt/*/bin (linux-x86_64 on Linux,
# windows-x86_64 on Windows/Git Bash) through find_tool. A release build
# with no llvm-strip fails loudly — it never ships an unstripped .so.
#
# SDK setup: the Android SDK root is taken from ANDROID_HOME (or
# ANDROID_SDK_ROOT), falling back to ~/workspace/android-sdk (the build
# machine's install). Build-tools default to 34.0.0 (override with
# ANDROID_BUILD_TOOLS_VERSION); if that version is not installed the
# newest installed build-tools is used instead. The platform jar is
# android-34, falling back to the newest installed platform. The JDK is
# taken from JAVA_HOME, falling back to the SDK tree's JDK 17
# (jdk-17.0.21.1+2), then to java on PATH. When a tool is genuinely
# missing the script fails with what to install — never a bare
# "not found".
#
# Windows/Git Bash (25 Nov 2026): ANDROID_HOME and JAVA_HOME often hold
# Windows-style roots (D:\AndroidSDK) there, and Windows build-tools
# ship apksigner and d8 as .bat files (aapt2/zipalign as .exe), so the
# SDK block below normalises every root to a POSIX path with cygpath
# when it exists and resolves every tool through find_tool (plain name,
# then .exe, then .bat, then .cmd) instead of assuming an extensionless
# executable. Without that, the apksigner check failed on Daniel's
# machine with a mixed path (D:\AndroidSDK/build-tools/36.0.0/apksigner)
# for a file that is really apksigner.bat.
#
# Archiver (25 Nov 2026): the APK contents are staged with `zip` when
# it is on PATH, else `7z` (7-Zip — its command is `7z`, NOT `zip`, so
# installing 7-Zip never satisfies a `zip` check; the old error message
# said exactly that and was wrong). `7z` is also looked for in the
# standard Windows install locations (C:\Program Files\7-Zip and
# C:\Program Files (x86)\7-Zip), so a default 7-Zip install works even
# if it is not on PATH. If neither is found the script fails with
# instructions for both routes instead of a bare "not found".
set -euo pipefail

RENDERER="${1:-}"
CONFIG="${2:-debug}"
CONFIG="${CONFIG,,}"
if [ "$RENDERER" != "opengl" ] && [ "$RENDERER" != "vulkan" ]; then
    echo "Usage: $0 <opengl|vulkan> [debug|release]" >&2
    exit 2
fi
if [ "$CONFIG" != "debug" ] && [ "$CONFIG" != "release" ]; then
    echo "Usage: $0 <opengl|vulkan> [debug|release] (config '$CONFIG' is not debug or release)" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- Android SDK / JDK resolution (portable across hosts) -------------------
# SDK root: ANDROID_HOME first, then ANDROID_SDK_ROOT, then the build
# machine's install. (The same resolution block lives in build-dex.sh —
# keep the two in sync.)
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

SDK_ROOT="$(posix_path "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/workspace/android-sdk}}")"
if [[ ! -d "$SDK_ROOT" ]]; then
    echo "ERROR: Android SDK not found at $SDK_ROOT." >&2
    echo "  Set ANDROID_HOME (or ANDROID_SDK_ROOT) to your SDK root, e.g.:" >&2
    echo "    export ANDROID_HOME=/path/to/android-sdk" >&2
    echo "  Install the SDK via Android Studio (SDK Manager) or:" >&2
    echo "    sdkmanager \"platform-tools\" \"platforms;android-34\" \"build-tools;34.0.0\"" >&2
    exit 1
fi

# Build-tools: requested version first, newest installed as fallback.
BT_VER="${ANDROID_BUILD_TOOLS_VERSION:-34.0.0}"
if [[ -d "$SDK_ROOT/build-tools/$BT_VER" ]]; then
    BT="$SDK_ROOT/build-tools/$BT_VER"
else
    _newest_bt="$(ls "$SDK_ROOT/build-tools" 2>/dev/null | sort -V | tail -1)"
    if [[ -n "$_newest_bt" && -d "$SDK_ROOT/build-tools/$_newest_bt" ]]; then
        echo "NOTE: build-tools $BT_VER not installed — using $_newest_bt instead."
        BT="$SDK_ROOT/build-tools/$_newest_bt"
        BT_VER="$_newest_bt"  # so later messages name the version in use
    else
        echo "ERROR: no Android build-tools installed under $SDK_ROOT/build-tools." >&2
        echo "  Install them with: sdkmanager \"build-tools;34.0.0\"" >&2
        exit 1
    fi
fi

# Platform jar: android-34 (must be >= targetSdk 34), newest installed
# as fallback.
PLATFORM_JAR="$SDK_ROOT/platforms/android-34/android.jar"
if [[ ! -f "$PLATFORM_JAR" ]]; then
    _newest_plat="$(ls -d "$SDK_ROOT"/platforms/android-* 2>/dev/null | sort -V | tail -1)"
    if [[ -n "$_newest_plat" && -f "$_newest_plat/android.jar" ]]; then
        echo "NOTE: platform android-34 not installed — using $(basename "$_newest_plat") instead."
        PLATFORM_JAR="$_newest_plat/android.jar"
    else
        echo "ERROR: no Android platform installed under $SDK_ROOT/platforms." >&2
        echo "  Install it with: sdkmanager \"platforms;android-34\"" >&2
        exit 1
    fi
fi

# JDK: JAVA_HOME first, then the SDK tree's Temurin JDK 17, then java on
# PATH. (javac + keytool are needed: the dex and the debug keystore.)
# JAVA_HOME is normalised the same way as the SDK root — it is a
# Windows-style path on Git Bash too.
JDK=""; JAVAC=""; KEYTOOL=""
if [[ -n "${JAVA_HOME:-}" ]]; then
    _jh="$(posix_path "$JAVA_HOME")"
    _javac="$(find_tool "$_jh/bin" javac || true)"
    if [[ -n "$_javac" ]]; then JDK="$_jh"; JAVAC="$_javac"; fi
fi
if [[ -z "$JDK" ]]; then
    _sdk_jdk="$SDK_ROOT/jdk-17.0.21.1+2"
    _javac="$(find_tool "$_sdk_jdk/bin" javac || true)"
    if [[ -n "$_javac" ]]; then JDK="$_sdk_jdk"; JAVAC="$_javac"; fi
fi
if [[ -z "$JDK" ]] && command -v javac &>/dev/null; then
    JDK="$(dirname "$(dirname "$(command -v javac)")")"
    JAVAC="$(command -v javac)"
fi
if [[ -z "$JDK" ]]; then
    echo "ERROR: no JDK found (need javac + keytool for the dex and the debug keystore)." >&2
    echo "  Set JAVA_HOME to a JDK 17 install, e.g.:" >&2
    echo "    export JAVA_HOME=/path/to/jdk-17" >&2
    echo "  Or install one via Android Studio (it bundles a JDK) or Temurin 17." >&2
    exit 1
fi
KEYTOOL="$(find_tool "$JDK/bin" keytool || true)"
if [[ -z "$KEYTOOL" ]] && command -v keytool &>/dev/null; then KEYTOOL="$(command -v keytool)"; fi
export PATH="$JDK/bin:$PATH"

# Tool checks with actionable errors (aapt2/zipalign/apksigner live in
# build-tools; keytool/javac in the JDK; zip or 7z stages the APK
# contents — see the archiver block below).
AAPT2="$(find_tool "$BT" aapt2 || true)"
ZIPALIGN="$(find_tool "$BT" zipalign || true)"
APKSIGNER="$(find_tool "$BT" apksigner || true)"
[[ -n "$AAPT2" ]] || { echo "ERROR: aapt2 missing in $BT — reinstall build-tools (sdkmanager \"build-tools;${BT_VER}\")." >&2; exit 1; }
[[ -n "$ZIPALIGN" ]] || { echo "ERROR: zipalign missing in $BT — reinstall build-tools (sdkmanager \"build-tools;${BT_VER}\")." >&2; exit 1; }
[[ -n "$APKSIGNER" ]] || { echo "ERROR: apksigner missing in $BT — reinstall build-tools (sdkmanager \"build-tools;${BT_VER}\")." >&2; exit 1; }
[[ -n "$JAVAC" ]] || { echo "ERROR: javac not found — set JAVA_HOME to a JDK 17 install." >&2; exit 1; }
[[ -n "$KEYTOOL" ]] || { echo "ERROR: keytool not found — set JAVA_HOME to a JDK 17 install." >&2; exit 1; }

# Archiver for staging the APK contents: `zip` first, else `7z`.
# 7-Zip does NOT provide a `zip` command — its command is `7z` — so a
# `zip` check can never be satisfied by installing 7-Zip alone.
# Resolution order (25 Nov 2026):
#   1. CPGE_7Z — an explicit override holding the path to the 7z
#      executable (e.g. CPGE_7Z='/d/Tools/7-Zip/7z.exe', or a directory
#      holding 7z.exe). The escape hatch for a renamed or moved 7-Zip
#      install; used when set and a 7z executable is found there.
#   2. `zip` on PATH.
#   3. `7z` on PATH.
#   4. Windows registry (when `reg` is available): 7-Zip records its
#      install directory in the Path value under
#      HKLM\SOFTWARE\7-Zip (64-bit) or HKLM\SOFTWARE\WOW6432Node\7-Zip
#      (32-bit). A missing `reg`, a missing key, or an unparsable value
#      is just "not found" — never a failure.
#   5. The standard install locations on C: and D: (D: is a common
#      drive for Windows program installs, e.g. D:\Program Files\7-Zip).
# A PATH change only reaches a NEW shell — a Git Bash window that was
# already open when PATH was edited still has the old PATH, so close
# and reopen Git Bash after editing PATH. The choice is printed so the
# log shows which archiver staged the APK.
ZIP_TOOL=""; ZIP_CMD=""
if [[ -n "${CPGE_7Z:-}" ]]; then
    if [[ -f "$CPGE_7Z" ]]; then
        ZIP_TOOL=7z; ZIP_CMD="$CPGE_7Z"
    elif [[ -d "$CPGE_7Z" ]]; then
        _z="$(find_tool "$CPGE_7Z" 7z || true)"
        if [[ -n "$_z" ]]; then ZIP_TOOL=7z; ZIP_CMD="$_z"; fi
    fi
    if [[ -z "$ZIP_TOOL" ]]; then
        echo "NOTE: CPGE_7Z is set ($CPGE_7Z) but no 7z executable was found there — ignoring it."
    fi
fi
if [[ -z "$ZIP_TOOL" ]] && command -v zip &>/dev/null; then
    ZIP_TOOL=zip; ZIP_CMD="$(command -v zip)"
fi
if [[ -z "$ZIP_TOOL" ]] && command -v 7z &>/dev/null; then
    ZIP_TOOL=7z; ZIP_CMD="$(command -v 7z)"
fi
if [[ -z "$ZIP_TOOL" ]] && command -v reg &>/dev/null; then
    for _key in "HKLM\SOFTWARE\7-Zip" "HKLM\SOFTWARE\WOW6432Node\7-Zip"; do
        _reg_path="$(reg query "$_key" /v Path 2>/dev/null | awk -F'REG_SZ' 'NF>1 {gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2; exit}' || true)"
        if [[ -n "$_reg_path" ]]; then
            _reg_dir="$(posix_path "$_reg_path")"
            _reg_dir="${_reg_dir%/}"
            _z="$(find_tool "$_reg_dir" 7z || true)"
            if [[ -n "$_z" ]]; then ZIP_TOOL=7z; ZIP_CMD="$_z"; break; fi
        fi
    done
fi
if [[ -z "$ZIP_TOOL" ]]; then
    for _dir in "/c/Program Files/7-Zip" "/c/Program Files (x86)/7-Zip" "/d/Program Files/7-Zip" "/d/Program Files (x86)/7-Zip"; do
        _z="$(find_tool "$_dir" 7z || true)"
        if [[ -n "$_z" ]]; then ZIP_TOOL=7z; ZIP_CMD="$_z"; break; fi
    done
fi
if [[ -z "$ZIP_TOOL" ]]; then
    echo "ERROR: no archiver found — needed to stage the APK contents (zip or 7z)." >&2
    echo "  7-Zip does NOT provide a \`zip\` command — its command is \`7z\`. Installing" >&2
    echo "  7-Zip alone never satisfies a \`zip\` check. Either:" >&2
    echo "    - get \`zip\` (on Git Bash: pacman -S zip), or" >&2
    echo "    - install 7-Zip and make sure \`7z\` is on PATH (add C:\\Program Files\\7-Zip" >&2
    echo "      to PATH). A PATH change only applies to a NEW Git Bash window — close" >&2
    echo "      and reopen Git Bash after editing PATH, or the old shell still has the" >&2
    echo "      old PATH. Or point the script straight at your 7z.exe:" >&2
    echo "        CPGE_7Z='/d/Tools/7-Zip/7z.exe'   (path to the 7z executable, any location)" >&2
    echo "  The script also looks up the 7-Zip install in the Windows registry and" >&2
    echo "  checks the standard install locations on C: and D:, so a default install" >&2
    echo "  usually just works." >&2
    exit 1
fi
echo "Archiver: $ZIP_TOOL ($ZIP_CMD)"

# Staging helpers — the same code path works with `zip` or `7z`, so the
# APK is identical in structure either way. apk_store adds files
# STORED (uncompressed — the .so and classes.dex are stored so zipalign
# can page-align them); apk_deflate adds a directory DEFLATED,
# recursively. Both run from the caller's working directory so the
# stored paths are relative, exactly as the old `zip` calls did. 7z
# output goes to /dev/null (it is chatty; errors still reach stderr).
apk_store() {  # apk_store <apk> <files...>
    local _apk="$1"; shift
    if [[ "$ZIP_TOOL" == "7z" ]]; then
        "$ZIP_CMD" a -tzip -mx=0 "$_apk" "$@" >/dev/null
    else
        zip -q -0 "$_apk" "$@"
    fi
}
apk_deflate() {  # apk_deflate <apk> <dir>
    local _apk="$1"; shift
    if [[ "$ZIP_TOOL" == "7z" ]]; then
        "$ZIP_CMD" a -tzip "$_apk" "$@" >/dev/null
    else
        zip -q -r "$_apk" "$@"
    fi
}

# --- NDK / llvm-strip (release only) ---------------------------------------
# Release builds are stripped of debug info. The NDK is resolved the same
# way Linux/cmake-build-android.sh does; llvm-strip comes from its
# toolchains/llvm/prebuilt/*/bin (one dir per host OS — globbed, so it
# works on Linux and on Windows/Git Bash). A release build with no
# llvm-strip fails loudly — it never ships an unstripped .so. Debug does
# not need the NDK at all, so nothing here runs for debug.
LLVM_STRIP=""
if [[ "$CONFIG" == "release" ]]; then
    newest_ndk_dir() {  # newest_ndk_dir <base> -> newest dir (directories only)
        ls -d "$1"/*/ 2>/dev/null | sort -V | tail -1
    }
    NDK_ROOT=""
    for _c in \
            "${ANDROID_NDK:-}" \
            "${ANDROID_NDK_HOME:-}" \
            "$(newest_ndk_dir "$SDK_ROOT/ndk")" \
            "$(newest_ndk_dir "$HOME/workspace/android-sdk/ndk")"; do
        if [[ -n "$_c" && -d "$_c" && -f "$_c/build/cmake/android.toolchain.cmake" ]]; then
            NDK_ROOT="$_c"
            break
        fi
    done
    if [[ -z "$NDK_ROOT" ]]; then
        echo "ERROR: Android NDK not found — needed for llvm-strip (release builds are stripped)." >&2
        echo "  Set ANDROID_NDK to your NDK root, e.g.:" >&2
        echo "    export ANDROID_NDK=/path/to/android-sdk/ndk/<version>" >&2
        exit 1
    fi
    _ndk_posix="$(posix_path "$NDK_ROOT")"
    for _pb in "$_ndk_posix"/toolchains/llvm/prebuilt/*/; do
        [[ -d "$_pb" ]] || continue
        _ls="$(find_tool "${_pb}bin" llvm-strip || true)"
        if [[ -n "$_ls" ]]; then LLVM_STRIP="$_ls"; break; fi
    done
    [[ -n "$LLVM_STRIP" ]] || { echo "ERROR: llvm-strip not found in $NDK_ROOT/toolchains/llvm/prebuilt/*/bin — release builds must be stripped." >&2; echo "  Install the NDK via Android Studio (SDK Manager → SDK Tools → NDK (Side by side)) or: sdkmanager \"ndk;<version>\"" >&2; exit 1; }
    echo "llvm-strip: $LLVM_STRIP"
fi

RENDERER_DIR=OpenGL
[ "$RENDERER" = "vulkan" ] && RENDERER_DIR=Vulkan
CONFIG_DIR=Debug
[ "$CONFIG" = "release" ] && CONFIG_DIR=Release
# 25 Nov 2026: prefer the build-dir .so for the requested config — it is
# the exact artifact for that config. The repo-root copy is only correct
# when it matches the requested config (a stale root copy from a debug
# build must never be packaged as a release APK), so it is the fallback
# now, not the first choice. (cmake-build-android.sh still installs the
# .so to the root on every build, and builds the APK AFTER that install,
# so the root copy is fresh when the script is called from a build.)
LIB="$REPO/Linux/build-android/arm64-v8a/$RENDERER_DIR/$CONFIG_DIR/libCPGE.so"
[ -f "$LIB" ] || LIB="$REPO/libCPGE.so"
MANIFEST="$REPO/Android/app/src/main/AndroidManifest.xml"
OUT="$SCRIPT_DIR/cpge-$RENDERER-$CONFIG.apk"
STAGE="$SCRIPT_DIR/stage-$RENDERER"

[ -f "$LIB" ] || { echo "Missing libCPGE.so — build the Android $RENDERER $CONFIG lib first (looked in $REPO/Linux/build-android/arm64-v8a/$RENDERER_DIR/$CONFIG_DIR/ and $REPO/libCPGE.so)." >&2; exit 1; }
[ -f "$MANIFEST" ] || { echo "Missing $MANIFEST" >&2; exit 1; }

# 1. Signing keystore. Release uses a release keystore when configured
# (CPGE_RELEASE_KEYSTORE + CPGE_RELEASE_KEYSTORE_PASS +
# CPGE_RELEASE_KEY_ALIAS + CPGE_RELEASE_KEY_PASS — all four required);
# otherwise it falls back to the debug keystore and says so, because a
# debug-signed release APK must not be mistaken for a distribution
# build. Debug always uses the debug keystore (created once, standard
# Android debug-keystore values).
SIGN_KEYSTORE="$SCRIPT_DIR/debug.keystore"
SIGN_STOREPASS=android
SIGN_ALIAS=androiddebugkey
SIGN_KEYPASS=android
if [[ "$CONFIG" == "release" && -n "${CPGE_RELEASE_KEYSTORE:-}" ]]; then
    SIGN_KEYSTORE="$CPGE_RELEASE_KEYSTORE"
    SIGN_STOREPASS="${CPGE_RELEASE_KEYSTORE_PASS:-}"
    SIGN_ALIAS="${CPGE_RELEASE_KEY_ALIAS:-}"
    SIGN_KEYPASS="${CPGE_RELEASE_KEY_PASS:-}"
    [[ -n "$SIGN_STOREPASS" && -n "$SIGN_ALIAS" && -n "$SIGN_KEYPASS" ]] || { echo "ERROR: CPGE_RELEASE_KEYSTORE is set but CPGE_RELEASE_KEYSTORE_PASS, CPGE_RELEASE_KEY_ALIAS and CPGE_RELEASE_KEY_PASS must all be set too." >&2; exit 1; }
    [[ -f "$SIGN_KEYSTORE" ]] || { echo "ERROR: release keystore not found: $SIGN_KEYSTORE (CPGE_RELEASE_KEYSTORE)." >&2; exit 1; }
    echo "Signing release APK with release keystore: $SIGN_KEYSTORE (alias $SIGN_ALIAS)"
elif [[ "$CONFIG" == "release" ]]; then
    echo "NOTE: release APK will be signed with the DEBUG keystore — set CPGE_RELEASE_KEYSTORE (+ CPGE_RELEASE_KEYSTORE_PASS, CPGE_RELEASE_KEY_ALIAS, CPGE_RELEASE_KEY_PASS) for a distribution keystore."
fi
if [[ ! -f "$SIGN_KEYSTORE" ]]; then
    "$KEYTOOL" -genkeypair -v \
        -keystore "$SIGN_KEYSTORE" -storepass "$SIGN_STOREPASS" -alias "$SIGN_ALIAS" -keypass "$SIGN_KEYPASS" \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug, O=Android, C=US" >/dev/null 2>&1
    echo "Created keystore: $SIGN_KEYSTORE"
fi

# 2. Compile the manifest + resources table. The SDK versions are passed
# explicitly: without them aapt2 assumes a low targetSdk and the APK gains
# implied legacy permissions (WRITE_EXTERNAL_STORAGE, READ_PHONE_STATE).
# --debug-mode only for debug: it marks the APK debuggable. A release APK
# must not be debuggable, so the flag is omitted for release (the
# manifest has no android:debuggable attribute).
rm -f "$OUT"
if [[ "$CONFIG" == "debug" ]]; then
    "$AAPT2" link -o "$OUT" \
        --manifest "$MANIFEST" \
        -I "$PLATFORM_JAR" \
        --min-sdk-version 26 --target-sdk-version 34 --debug-mode
else
    "$AAPT2" link -o "$OUT" \
        --manifest "$MANIFEST" \
        -I "$PLATFORM_JAR" \
        --min-sdk-version 26 --target-sdk-version 34
fi

# 3. Stage the native library and the game assets. AIHistory holds runtime
# AI log files, not game data, so it is excluded. The .so is stored
# uncompressed (zipalign then page-aligns it, which the loader requires);
# assets are deflated.
rm -rf "$STAGE"
mkdir -p "$STAGE/lib/arm64-v8a" "$STAGE/assets"
cp "$LIB" "$STAGE/lib/arm64-v8a/libCPGE.so"
# Release: strip the STAGED copy (not the source — the build-dir and
# root copies are left alone for the caller). llvm-strip was resolved
# and required earlier; a missing one already failed the script.
if [[ "$CONFIG" == "release" ]]; then
    _staged="$STAGE/lib/arm64-v8a/libCPGE.so"
    _before="$(stat -c%s "$_staged")"
    "$LLVM_STRIP" "$_staged"
    _after="$(stat -c%s "$_staged")"
    echo "Stripped: $_staged (${_before} -> ${_after} bytes)"
fi
cp -r "$REPO/Assets/." "$STAGE/assets/"
rm -rf "$STAGE/assets/AIHistory"
( cd "$STAGE" && apk_store "$OUT" lib/arm64-v8a/libCPGE.so && apk_deflate "$OUT" assets )

# 4. Optional Java helper dex (TTS JNI bridge, step 3). Present only after
# build-dex.sh has run AND the manifest's android:hasCode is true; until
# then the APK ships without a dex and this step is skipped.
if [ -f "$SCRIPT_DIR/classes.dex" ]; then
    ( cd "$SCRIPT_DIR" && apk_store "$OUT" classes.dex )
    echo "Added classes.dex (TTS JNI helper)"
else
    echo "NOTE: no classes.dex — TTS JNI helper not packaged (run build-dex.sh first)"
fi
rm -rf "$STAGE"

# 5. Align then sign. zipalign must run before apksigner (signing covers
# the aligned bytes).
"$ZIPALIGN" -f 4 "$OUT" "$OUT.aligned"
mv "$OUT.aligned" "$OUT"
"$APKSIGNER" sign --ks "$SIGN_KEYSTORE" --ks-pass "pass:$SIGN_STOREPASS" --ks-key-alias "$SIGN_ALIAS" --key-pass "pass:$SIGN_KEYPASS" "$OUT"

echo "=== $OUT ==="
ls -la "$OUT"
"$APKSIGNER" verify --verbose --print-certs "$OUT"
"$AAPT2" dump badging "$OUT" | head -8
