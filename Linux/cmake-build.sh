#!/usr/bin/env bash
# cmake-build.sh  <renderer> [debug|release]
#   renderer : opengl | vulkan
#   config   : debug | release  (default: debug)
#
# Examples:
#   ./cmake-build.sh opengl
#   ./cmake-build.sh vulkan release
#   ./cmake-build.sh clean

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_EXE="${CMAKE_EXE:-cmake}"

ARG1="${1:-}"
ARG2="${2:-}"

# --- Clean ---
if [[ "${ARG1,,}" == "clean" ]]; then
    echo "Cleaning Linux build directories..."
    rm -rf "${SCRIPT_DIR}/build"
    echo "Clean complete."
    exit 0
fi

# --- Usage ---
if [[ -z "${ARG1}" ]]; then
    echo ""
    echo "Usage: $(basename "$0") <renderer> [debug|release]"
    echo "  Renderers : opengl  vulkan"
    echo "  Config    : debug  release  (default: debug)"
    echo ""
    echo "Examples:"
    echo "  $(basename "$0") opengl"
    echo "  $(basename "$0") vulkan release"
    echo "  $(basename "$0") clean"
    exit 1
fi

# --- Map renderer argument ---
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
        echo "Valid renderers: opengl  vulkan  (DirectX is Windows-only)"
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

# --- Validate cmake ---
if ! command -v "${CMAKE_EXE}" &>/dev/null; then
    echo "ERROR: cmake not found. Install it with:"
    echo "  sudo apt install cmake          # Ubuntu/Debian"
    echo "  sudo dnf install cmake          # Fedora/RHEL"
    echo "  sudo pacman -S cmake            # Arch"
    echo "Or set CMAKE_EXE to the full path of your cmake binary."
    exit 1
fi

# --- Patch Includes.h (Linux section) ----------------------------------------
# The Windows section uses a different indent pattern and is left untouched.
# We use Python3 to comment out all Linux/Android renderer defines then
# uncomment only the one requested — so IDEs on Linux pick up the right renderer.
INCLUDES_H="${PROJECT_ROOT}/Includes.h"
if command -v python3 &>/dev/null && [[ -f "${INCLUDES_H}" ]]; then
    echo "Patching Includes.h: enabling ${RENDERER_DEFINE} on Linux..."
    python3 - "${RENDERER_DEFINE}" "${INCLUDES_H}" <<'PYEOF'
import re, sys
define = sys.argv[1]          # e.g. "__USE_VULKAN__"
path   = sys.argv[2]
with open(path, 'r') as f:
    t = f.read()
# Linux/Android defines have the form:  "    //#define __USE_X__"
# (4-space indent, no gap between // and #define).
# First comment everything out (idempotent), then uncomment the chosen one.
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
BUILD_DIR="${SCRIPT_DIR}/build/${RENDERER}/${CONFIG}"
mkdir -p "${BUILD_DIR}"

echo ""
echo "Building: OS=Linux  Renderer=${RENDERER}  Config=${CONFIG}  Dir=${BUILD_DIR}"
echo ""

"${CMAKE_EXE}" \
    -S "${SCRIPT_DIR}" \
    -B "${BUILD_DIR}" \
    -DRENDERER:STRING="${RENDERER}" \
    -DCMAKE_BUILD_TYPE="${CONFIG}"

"${CMAKE_EXE}" --build "${BUILD_DIR}" --parallel "$(nproc 2>/dev/null || echo 4)"

echo ""
echo "Build succeeded: ${RENDERER} ${CONFIG} -- $(date)"
