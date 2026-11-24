#!/bin/bash
# package-apk.sh — manual aapt2 APK packaging for CPGE Android (Phase D step 3, 24 Nov 2026; moved into the repo at Android/ on 25 Nov 2026).
#
# Gradle cannot run in this VM (its client<->daemon loopback TCP is
# intercepted by the sandbox network policy), and Daniel has decided
# (24 Nov 2026) that manual aapt2 packaging is the PRIMARY path and
# Gradle is the fallback only — do not wait on the other_tcp setting.
# This script is therefore how APKs are produced, not a stopgap.
#
# Usage: ./package-apk.sh <opengl|vulkan>   (run from Android/ or anywhere;
#        all paths are resolved from this script's location)
# Output: Android/cpge-<renderer>-debug.apk (signed, zipaligned).
# The Java TTS helper (CpgeTts) is compiled to classes.dex by
# build-dex.sh (same folder) and added automatically when it exists; the
# manifest's android:hasCode is true, so the dex must be present for the
# TTS class to load (before step 3 the APK shipped without a dex).
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
set -euo pipefail

RENDERER="${1:-}"
if [ "$RENDERER" != "opengl" ] && [ "$RENDERER" != "vulkan" ]; then
    echo "Usage: $0 <opengl|vulkan>" >&2
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
# build-tools; keytool/javac in the JDK; zip stages the APK contents).
AAPT2="$(find_tool "$BT" aapt2 || true)"
ZIPALIGN="$(find_tool "$BT" zipalign || true)"
APKSIGNER="$(find_tool "$BT" apksigner || true)"
[[ -n "$AAPT2" ]] || { echo "ERROR: aapt2 missing in $BT — reinstall build-tools (sdkmanager \"build-tools;${BT_VER}\")." >&2; exit 1; }
[[ -n "$ZIPALIGN" ]] || { echo "ERROR: zipalign missing in $BT — reinstall build-tools (sdkmanager \"build-tools;${BT_VER}\")." >&2; exit 1; }
[[ -n "$APKSIGNER" ]] || { echo "ERROR: apksigner missing in $BT — reinstall build-tools (sdkmanager \"build-tools;${BT_VER}\")." >&2; exit 1; }
[[ -n "$JAVAC" ]] || { echo "ERROR: javac not found — set JAVA_HOME to a JDK 17 install." >&2; exit 1; }
[[ -n "$KEYTOOL" ]] || { echo "ERROR: keytool not found — set JAVA_HOME to a JDK 17 install." >&2; exit 1; }
command -v zip &>/dev/null || { echo "ERROR: zip not found — needed to stage the APK contents (on Git Bash: pacman -S zip, or install 7-Zip and put it on PATH)." >&2; exit 1; }

RENDERER_DIR=OpenGL
[ "$RENDERER" = "vulkan" ] && RENDERER_DIR=Vulkan
# 25 Nov 2026: prefer the repo-root installed copy — cmake-build-android.sh
# installs libCPGE.so to the project root on every successful build, so the
# APK packages exactly the library the root run uses (and the APK is built
# AFTER that install). Fall back to the build-dir .so when the root copy is
# absent (e.g. the library was built by Gradle or an older script).
LIB="$REPO/libCPGE.so"
[ -f "$LIB" ] || LIB="$REPO/Linux/build-android/arm64-v8a/$RENDERER_DIR/Debug/libCPGE.so"
MANIFEST="$REPO/Android/app/src/main/AndroidManifest.xml"
OUT="$SCRIPT_DIR/cpge-$RENDERER-debug.apk"
KEYSTORE="$SCRIPT_DIR/debug.keystore"
STAGE="$SCRIPT_DIR/stage-$RENDERER"

[ -f "$LIB" ] || { echo "Missing libCPGE.so — build the Android $RENDERER lib first (looked in $REPO/libCPGE.so and $REPO/Linux/build-android/arm64-v8a/$RENDERER_DIR/Debug/)." >&2; exit 1; }
[ -f "$MANIFEST" ] || { echo "Missing $MANIFEST" >&2; exit 1; }

# 1. Debug keystore (created once; standard Android debug-keystore values).
if [ ! -f "$KEYSTORE" ]; then
    "$KEYTOOL" -genkeypair -v \
        -keystore "$KEYSTORE" -storepass android -alias androiddebugkey -keypass android \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug, O=Android, C=US" >/dev/null 2>&1
    echo "Created debug keystore: $KEYSTORE"
fi

# 2. Compile the manifest + resources table. The SDK versions are passed
# explicitly: without them aapt2 assumes a low targetSdk and the APK gains
# implied legacy permissions (WRITE_EXTERNAL_STORAGE, READ_PHONE_STATE).
rm -f "$OUT"
"$AAPT2" link -o "$OUT" \
    --manifest "$MANIFEST" \
    -I "$PLATFORM_JAR" \
    --min-sdk-version 26 --target-sdk-version 34 --debug-mode

# 3. Stage the native library and the game assets. AIHistory holds runtime
# AI log files, not game data, so it is excluded. The .so is stored
# uncompressed (zipalign then page-aligns it, which the loader requires);
# assets are deflated.
rm -rf "$STAGE"
mkdir -p "$STAGE/lib/arm64-v8a" "$STAGE/assets"
cp "$LIB" "$STAGE/lib/arm64-v8a/libCPGE.so"
cp -r "$REPO/Assets/." "$STAGE/assets/"
rm -rf "$STAGE/assets/AIHistory"
( cd "$STAGE" && zip -q -0 "$OUT" lib/arm64-v8a/libCPGE.so && zip -q -r "$OUT" assets )

# 4. Optional Java helper dex (TTS JNI bridge, step 3). Present only after
# build-dex.sh has run AND the manifest's android:hasCode is true; until
# then the APK ships without a dex and this step is skipped.
if [ -f "$SCRIPT_DIR/classes.dex" ]; then
    ( cd "$SCRIPT_DIR" && zip -q -0 "$OUT" classes.dex )
    echo "Added classes.dex (TTS JNI helper)"
else
    echo "NOTE: no classes.dex — TTS JNI helper not packaged (run build-dex.sh first)"
fi
rm -rf "$STAGE"

# 5. Align then sign. zipalign must run before apksigner (signing covers
# the aligned bytes).
"$ZIPALIGN" -f 4 "$OUT" "$OUT.aligned"
mv "$OUT.aligned" "$OUT"
"$APKSIGNER" sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android "$OUT"

echo "=== $OUT ==="
ls -la "$OUT"
"$APKSIGNER" verify --verbose --print-certs "$OUT"
"$AAPT2" dump badging "$OUT" | head -8
