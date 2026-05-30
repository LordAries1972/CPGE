#!/usr/bin/env bash
# cmake-build-android.sh  <renderer> [debug|release] [abi]
#   renderer : opengl | vulkan
#   config   : debug | release  (default: debug)
#   abi      : arm64-v8a | x86_64 | armeabi-v7a | x86  (default: arm64-v8a)
#
# Requires ANDROID_NDK (or ANDROID_NDK_HOME / ANDROID_HOME) environment variable
# pointing to the root of the Android NDK.
#
# Examples:
#   ./cmake-build-android.sh opengl
#   ./cmake-build-android.sh vulkan release arm64-v8a
#   ./cmake-build-android.sh clean

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_EXE="${CMAKE_EXE:-cmake}"

ARG1="${1:-}"
ARG2="${2:-}"
ARG3="${3:-}"

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
    echo "Usage: $(basename "$0") <renderer> [debug|release] [abi]"
    echo "  Renderers : opengl  vulkan"
    echo "  Config    : debug  release        (default: debug)"
    echo "  ABI       : arm64-v8a  x86_64  armeabi-v7a  x86  (default: arm64-v8a)"
    echo ""
    echo "Environment variables:"
    echo "  ANDROID_NDK        Path to the Android NDK root"
    echo "  ANDROID_PLATFORM   Android API level (default: android-24)"
    echo "  CMAKE_EXE          Path to cmake binary (default: cmake)"
    echo ""
    echo "Examples:"
    echo "  $(basename "$0") opengl"
    echo "  $(basename "$0") vulkan release arm64-v8a"
    echo "  $(basename "$0") clean"
    exit 1
fi

# --- Locate Android NDK ---
NDK_ROOT=""
for _candidate in \
        "${ANDROID_NDK:-}" \
        "${ANDROID_NDK_HOME:-}" \
        "${ANDROID_HOME:-}/ndk-bundle" \
        "${ANDROID_HOME:-}/ndk/$(ls "${ANDROID_HOME:-/nonexistent}/ndk" 2>/dev/null | sort -V | tail -1)"; do
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
RENDERER_DEFINE=""
case "${ARG1,,}" in
    opengl)
        RENDERER="OpenGL"
        RENDERER_DEFINE="__USE_OPENGL__"
        ;;
    vulkan)
        RENDERER="Vulkan"
        RENDERER_DEFINE="__USE_VULKAN__"
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
ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-24}"

# --- Validate cmake ---
if ! command -v "${CMAKE_EXE}" &>/dev/null; then
    echo "ERROR: cmake not found."
    echo "Install it via Android Studio's bundled cmake, or:"
    echo "  sdkmanager 'cmake;<version>'"
    exit 1
fi

# --- Patch Includes.h (Android section) --------------------------------------
INCLUDES_H="${PROJECT_ROOT}/Includes.h"
if command -v python3 &>/dev/null && [[ -f "${INCLUDES_H}" ]]; then
    echo "Patching Includes.h: enabling ${RENDERER_DEFINE} on Android..."
    python3 - "${RENDERER_DEFINE}" "${INCLUDES_H}" <<'PYEOF'
import re, sys
define = sys.argv[1]
path   = sys.argv[2]
with open(path, 'r') as f:
    t = f.read()
for d in ('OPENGL', 'VULKAN'):
    t = re.sub(r'(?m)^([ \t]*)(?://)?(#define __USE_' + d + r'__\b.*)', r'\1//\2', t)
t = re.sub(r'(?m)^([ \t]*)//(' + re.escape('#define ' + define) + r'\b.*)', r'\1\2', t)
with open(path, 'w') as f:
    f.write(t)
PYEOF
    echo "Includes.h patched."
else
    echo "INFO: python3 not found or Includes.h missing — skipping Includes.h patch."
    echo "      The renderer define is passed via -D${RENDERER_DEFINE} by CMake."
fi

# --- Build ---
BUILD_DIR="${SCRIPT_DIR}/build-android/${ABI}/${RENDERER}/${CONFIG}"
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
    -DCMAKE_BUILD_TYPE="${CONFIG}"

"${CMAKE_EXE}" --build "${BUILD_DIR}" --parallel "$(nproc 2>/dev/null || echo 4)"

echo ""
echo "Build succeeded: Android/${ABI} ${RENDERER} ${CONFIG} -- $(date)"
echo ""
echo "Output shared library:"
find "${BUILD_DIR}" -name "lib*.so" -not -path "*/CMakeFiles/*" 2>/dev/null || true
echo ""
echo "Next steps:"
echo "  1. Copy the .so to your Android project's jniLibs/${ABI}/"
echo "  2. Copy Assets/ to your APK's assets/ folder"
echo "  3. Set up NativeActivity or GameActivity in your AndroidManifest.xml"
