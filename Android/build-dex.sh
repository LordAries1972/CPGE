#!/bin/bash
# build-dex.sh — compile the CPGE Android Java helpers to classes.dex
# (Phase D step 3, 24 Nov 2026; moved into the repo at Android/ on 25 Nov 2026).
#
# Gradle cannot run in this VM (its client<->daemon loopback TCP is
# intercepted by the sandbox network policy), so the Java side is built by
# hand: javac against android.jar, then d8 to a dex file. package-apk.sh
# (same folder) adds classes.dex to the APK automatically when this file
# exists. Daniel decided (24 Nov 2026) that manual aapt2 packaging is the
# primary path and Gradle the fallback only, so this hand-built dex is how
# the TTS helper ships; when Gradle works again, the same sources under
# Android/app/src/main/java are picked up by the Gradle build
# automatically — nothing here duplicates that path.
#
# Usage: ./build-dex.sh
# Output: Android/classes.dex
#
# SDK setup: same as package-apk.sh — SDK root from ANDROID_HOME (or
# ANDROID_SDK_ROOT), build-tools from ANDROID_BUILD_TOOLS_VERSION
# (default 34.0.0, newest installed as fallback), JDK from JAVA_HOME,
# then the SDK tree's JDK 17, then java on PATH.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- Android SDK / JDK resolution (portable across hosts) -------------------
# (The same resolution block lives in package-apk.sh — keep the two in sync.)
#
# Git Bash on Windows hands us Windows-style roots (D:\AndroidSDK) via
# ANDROID_HOME/JAVA_HOME, and Windows build-tools ship d8 as d8.bat
# (see package-apk.sh for the full story) — so roots are normalised to
# POSIX paths with cygpath when it exists and tools are resolved through
# find_tool (plain name, then .exe, then .bat, then .cmd).
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

JDK=""; JAVAC=""
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
    echo "ERROR: no JDK found (need javac + d8 for the dex)." >&2
    echo "  Set JAVA_HOME to a JDK 17 install, e.g.:" >&2
    echo "    export JAVA_HOME=/path/to/jdk-17" >&2
    echo "  Or install one via Android Studio (it bundles a JDK) or Temurin 17." >&2
    exit 1
fi
export PATH="$JDK/bin:$PATH"

D8="$(find_tool "$BT" d8 || true)"
[[ -n "$D8" ]] || { echo "ERROR: d8 missing in $BT — reinstall build-tools (sdkmanager \"build-tools;${BT_VER}\")." >&2; exit 1; }
[[ -n "$JAVAC" ]] || { echo "ERROR: javac not found — set JAVA_HOME to a JDK 17 install." >&2; exit 1; }

JAVA_SRC="$REPO/Android/app/src/main/java"
OUT_CLASSES="$SCRIPT_DIR/dex-classes"
OUT_DEX="$SCRIPT_DIR/classes.dex"

[ -d "$JAVA_SRC" ] || { echo "No Java sources at $JAVA_SRC" >&2; exit 1; }
[ -f "$PLATFORM_JAR" ] || { echo "Missing android.jar" >&2; exit 1; }

rm -rf "$OUT_CLASSES"
mkdir -p "$OUT_CLASSES"
# --release 17 would reject the android.jar stubs (compiled for an older
# release), so compile against the platform jar with source/target 17.
# -encoding UTF-8: the comments use em-dashes and javac otherwise
# defaults to US-ASCII in this environment and fails on them.
"$JAVAC" -encoding UTF-8 -source 17 -target 17 -nowarn \
    -cp "$PLATFORM_JAR" \
    -d "$OUT_CLASSES" \
    $(find "$JAVA_SRC" -name "*.java")
echo "Compiled: $(find "$OUT_CLASSES" -name "*.class" | wc -l) class file(s)"

rm -f "$OUT_DEX"
# d8 converts the .class files to a single classes.dex. --min-api 26
# matches minSdk (AAudio needs API 26); the dex is stored uncompressed
# in the APK by package-apk.sh.
"$D8" --min-api 26 --output "$SCRIPT_DIR" "$OUT_CLASSES"/com/ultimanium/cpge/*.class
# d8 writes classes.dex next to --output; move it to the expected name
# (it already is classes.dex when --output is a directory).
ls -la "$OUT_DEX"
rm -rf "$OUT_CLASSES"
echo "Dex ready: $OUT_DEX"
