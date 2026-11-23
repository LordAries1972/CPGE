#!/usr/bin/env bash
# cmake-build.sh  <renderer> [debug|release]
#   renderer : opengl | vulkan | all
#   config   : debug | release  (default: debug)
#
# Examples:
#   ./cmake-build.sh opengl
#   ./cmake-build.sh vulkan release
#   ./cmake-build.sh all debug          # builds every pipeline in Debug
#   ./cmake-build.sh all release        # builds every pipeline in Release
#   ./cmake-build.sh clean
#   ./cmake-build.sh help               # lists every directive (also -h, --help)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_EXE="${CMAKE_EXE:-cmake}"

# Split the command line into the positional arguments and the optional -clean switch (any position).
# Without -clean the existing build tree (objects, CMake cache, binaries) is reused, so only changed sources recompile.
CLEAN_BUILD=""
POSARGS=()
for a in "$@"; do
    if [[ "${a,,}" == "-clean" ]]; then CLEAN_BUILD=1; else POSARGS+=("$a"); fi
done
ARG1="${POSARGS[0]:-}"
ARG2="${POSARGS[1]:-}"

# --- Help (lists every directive and exits) ---
show_help() {
    local me
    me="$(basename "$0")"
    cat <<EOF

${me} - configure and build the engine for Linux with CMake

Usage:
  ${me} <directive> [config] [-clean]

Build directives (one render pipeline):
  opengl    OpenGL renderer
  vulkan    Vulkan renderer
            (DirectX 11/12 are Windows-only - use cmake-build.bat)

Multi-pipeline directive:
  all       Builds opengl then vulkan with the given config and prints a
            pass/fail summary. Exits 1 if any pipeline failed.

Option (any position, used by every build directive and by all):
  -clean    Deletes the build tree of the pipeline being built (objects, CMake
            cache, binaries) first for a fresh compile. Without it the existing
            binaries are reused (incremental build).

Maintenance directives:
  clean     Deletes the Linux build directory (Linux/build).
  help      Shows this list. Aliases: -h  --help

Config (second argument, used by opengl / vulkan / all):
  debug     Debug build   (default when omitted)
  release   Release build

What a build does:
  1. Reads GAME_NAME from Includes.h.
  2. Configures and builds into Linux/build/<Renderer>/<Config>
     using all CPU cores.
  NOTE: this script never edits Includes.h. The renderer define
  (__USE_OPENGL__ / __USE_VULKAN__) is passed to the compiler by CMake
  (-D via Linux/CMakeLists.txt), which is the single source of truth.
  An earlier version of this script patched Includes.h with a global
  regex; that mutated the working tree and uncommented the define in
  every platform section, so it was removed.

Environment:
  CMAKE_EXE   Path to the cmake binary (default: cmake on PATH).

Examples:
  ${me} opengl
  ${me} vulkan release
  ${me} all debug
  ${me} all release
  ${me} opengl release -clean   # fresh full compile
  ${me} clean
  ${me} help

EOF
}

case "${ARG1,,}" in
    help|-h|--help)
        show_help
        exit 0
        ;;
esac

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
    echo "Usage: $(basename "$0") <renderer> [debug|release] [-clean]"
    echo "  Renderers : opengl  vulkan"
    echo "  Special   : all   (builds every pipeline in the specified config)"
    echo "  Config    : debug  release  (default: debug)"
    echo ""
    echo "Examples:"
    echo "  $(basename "$0") opengl"
    echo "  $(basename "$0") vulkan release"
    echo "  $(basename "$0") all debug"
    echo "  $(basename "$0") all release"
    echo "  $(basename "$0") clean"
    echo ""
    echo "Run '$(basename "$0") help' for the full list of directives."
    exit 1
fi

# --- Build ALL pipelines ---
# Iterates over every Linux-supported renderer, calling this script for each
# one so the full build steps run as normal.
# Uses 'if ! command' to suppress set -e so a failing pipeline doesn't abort
# the remaining ones; a summary is printed and exit 1 if any build failed.
if [[ "${ARG1,,}" == "all" ]]; then
    ALL_CONFIG="debug"
    [[ "${ARG2,,}" == "release" ]] && ALL_CONFIG="release"

    echo ""
    echo "============================================================"
    echo " Building ALL render pipelines  --  Config: ${ALL_CONFIG}"
    echo "============================================================"
    echo ""

    ALL_FAIL_COUNT=0
    ALL_PASS_LIST=""
    ALL_FAIL_LIST=""
    IS_FIRST_BUILD=1

    for renderer in opengl vulkan; do
        echo ""
        echo "  ---- Building ${renderer} ${ALL_CONFIG} ----"
        if [[ "${IS_FIRST_BUILD}" == "1" ]]; then
            if "${BASH_SOURCE[0]}" "${renderer}" "${ALL_CONFIG}" ${CLEAN_BUILD:+-clean}; then
                ALL_PASS_LIST="${ALL_PASS_LIST} ${renderer}"
            else
                ALL_FAIL_COUNT=$((ALL_FAIL_COUNT + 1))
                ALL_FAIL_LIST="${ALL_FAIL_LIST} ${renderer}"
            fi
            IS_FIRST_BUILD=0
        else
            if SKIP_VERSION_INCREMENT=1 "${BASH_SOURCE[0]}" "${renderer}" "${ALL_CONFIG}" ${CLEAN_BUILD:+-clean}; then
                ALL_PASS_LIST="${ALL_PASS_LIST} ${renderer}"
            else
                ALL_FAIL_COUNT=$((ALL_FAIL_COUNT + 1))
                ALL_FAIL_LIST="${ALL_FAIL_LIST} ${renderer}"
            fi
        fi
    done

    echo ""
    echo "============================================================"
    echo " ALL BUILD SUMMARY  --  $(date)"
    [[ -n "${ALL_PASS_LIST}" ]] && echo "  PASSED :${ALL_PASS_LIST}"
    [[ -n "${ALL_FAIL_LIST}" ]] && echo "  FAILED :${ALL_FAIL_LIST}"
    echo "============================================================"
    echo ""

    if [[ ${ALL_FAIL_COUNT} -gt 0 ]]; then
        exit 1
    fi
    exit 0
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

# --- Renderer define: CMake only, no Includes.h patching -------------------
# The renderer define (__USE_OPENGL__ / __USE_VULKAN__) is passed to the
# compiler by CMake (Linux/CMakeLists.txt, -D). Do NOT patch Includes.h here:
# a global regex would uncomment the define in the Linux, Android AND macOS
# sections at once and dirty the working tree on every build. The commented-out
# defines in the Linux/Android sections of Includes.h are intentional — CMake
# is the single source of truth for the active renderer.
# (RENDERER_DEFINE is kept for the informational echo below.)

# --- Extract GAME_NAME from Includes.h ---
GAME_NAME_VAL=$(sed -n 's/^#define GAME_NAME[[:space:]]*"\([^"]*\)".*/\1/p' "${PROJECT_ROOT}/Includes.h" | head -1)
if [[ -z "${GAME_NAME_VAL}" ]]; then
    echo "WARNING: Could not extract GAME_NAME from Includes.h -- defaulting to UNKNOWN"
    GAME_NAME_VAL="UNKNOWN"
fi

# --- Build ---
BUILD_DIR="${SCRIPT_DIR}/build/${RENDERER}/${CONFIG}"
# -clean: wipe this pipeline's build tree so the compile below starts completely fresh.
if [[ -n "${CLEAN_BUILD}" && -d "${BUILD_DIR}" ]]; then
    echo "-clean: removing ${BUILD_DIR} ..."
    rm -rf "${BUILD_DIR}"
    if [[ -d "${BUILD_DIR}" ]]; then echo "ERROR: Could not remove ${BUILD_DIR}"; exit 1; fi
fi
mkdir -p "${BUILD_DIR}"

echo ""
echo "Building: OS=Linux  Renderer=${RENDERER}  GameName=${GAME_NAME_VAL}  Config=${CONFIG}  Dir=${BUILD_DIR}"
echo ""

"${CMAKE_EXE}" \
    -S "${SCRIPT_DIR}" \
    -B "${BUILD_DIR}" \
    -DRENDERER:STRING="${RENDERER}" \
    -DGAME_NAME:STRING="${GAME_NAME_VAL}" \
    -DCMAKE_BUILD_TYPE="${CONFIG}"

"${CMAKE_EXE}" --build "${BUILD_DIR}" --parallel "$(nproc 2>/dev/null || echo 4)"

echo ""
echo "Build succeeded: ${RENDERER} ${CONFIG} -- $(date)"
