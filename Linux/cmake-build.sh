#!/usr/bin/env bash
# cmake-build.sh  <renderer> [debug|release]
#   renderer : opengl | vulkan | all
#   config   : debug | release  (default: debug)
#
# Options (any position):
#   -clean      fresh compile (wipe the pipeline's build tree first)
#   -warnings  show compiler warnings (off by default — builds are quiet)
#
# Examples:
#   ./cmake-build.sh opengl
#   ./cmake-build.sh vulkan release
#   ./cmake-build.sh all debug          # builds every pipeline in Debug
#   ./cmake-build.sh all release        # builds every pipeline in Release
#   ./cmake-build.sh opengl -warnings   # show compiler warnings
#   ./cmake-build.sh clean
#   ./cmake-build.sh help               # lists every directive (also -h, --help, -help)
#
# On success the built executable and its shaders are installed into the
# project root (like install-debug.bat / install-release.bat on Windows),
# so the executable runs from the root where Assets/ lives.

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

# --- Help (lists every directive and exits) ---
show_help() {
    local me
    me="$(basename "$0")"
    cat <<EOF

${me} - configure and build the engine for Linux with CMake

Usage:
  ${me} <directive> [config] [-clean] [-warnings]

Build directives (one render pipeline):
  opengl    OpenGL renderer
  vulkan    Vulkan renderer
            (DirectX 11/12 are Windows-only - use cmake-build.bat)

Multi-pipeline directive:
  all       Builds opengl then vulkan with the given config and prints a
            pass/fail summary. Exits 1 if any pipeline failed.

Options (any position, used by every build directive and by all):
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
  clean     Deletes the Linux build directory (Linux/build).
  help      Shows this list. Aliases: -h  --help  -help

Config (second argument, used by opengl / vulkan / all):
  debug     Debug build   (default when omitted)
  release   Release build

What a build does:
  1. Reads GAME_NAME from Includes.h.
  2. Configures and builds into Linux/build/<Renderer>/<Config>
     using all CPU cores.
  3. On success, installs the built executable and its shaders
     into the project root — the same step install-debug.bat /
     install-release.bat do on Windows — so the executable runs
     from the root, where Assets/ and the other runtime folders
     are found. For 'all' the install runs once at the end, and
     only if every pipeline succeeded.
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
  ${me} opengl -warnings        # show compiler warnings
  ${me} all debug -warnings
  ${me} clean
  ${me} help

EOF
}

# Help is checked first (any argument position), so it works even when
# cmake is not installed — same as cmake-build.bat.
for _a in "$@"; do
    case "${_a,,}" in
        help|-h|--help|-help)
            show_help
            exit 0
            ;;
    esac
done

# --- Install to project root (mirrors Windows install-debug/install-release.bat) ---
# Copies the built executable and its shaders from the build tree to the
# project root, so the executable runs from the root where Assets/ and the
# other runtime folders live. Runs on success only.
# The binary and *.glsl are always copied (the .glsl files are identical
# to the tracked root copies). *.spv / *.cso are copied only when the root
# copy is missing: the Linux-compiled .spv differ from the tracked root
# ones only in embedded source paths (same size, same code), so copying
# them on every build would dirty two tracked files for no functional
# gain — the root already holds working copies, and shaderc (when found)
# can also compile GLSL to SPIR-V at runtime.
install_to_root() {
    local _renderer="$1" _config="$2"
    local _dir="${SCRIPT_DIR}/build/${_renderer}/${_config}"
    local _bin="${_dir}/${_renderer}CPGE"
    if [[ -f "${_bin}" ]]; then
        cp -f "${_bin}" "${PROJECT_ROOT}/${_renderer}CPGE" || { echo "ERROR: could not install ${_bin} to the project root"; return 1; }
        echo "Installed: ${PROJECT_ROOT}/${_renderer}CPGE"
    else
        echo "WARNING: expected binary not found, skipping install: ${_bin}"
        return 0
    fi
    local _shader _base
    for _shader in "${_dir}"/*.glsl "${_dir}"/*.spv "${_dir}"/*.cso; do
        [[ -f "${_shader}" ]] || continue
        _base="$(basename "${_shader}")"
        case "${_shader}" in
            *.glsl)
                cp -f "${_shader}" "${PROJECT_ROOT}/${_base}" || { echo "ERROR: could not install ${_base} to the project root"; return 1; }
                echo "Installed: ${PROJECT_ROOT}/${_base}"
                ;;
            *)
                if [[ ! -f "${PROJECT_ROOT}/${_base}" ]]; then
                    cp -f "${_shader}" "${PROJECT_ROOT}/${_base}" || { echo "ERROR: could not install ${_base} to the project root"; return 1; }
                    echo "Installed: ${PROJECT_ROOT}/${_base}"
                fi
                ;;
        esac
    done
    return 0
}

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
    echo "Usage: $(basename "$0") <renderer> [debug|release] [-clean] [-warnings]"
    echo "  Renderers : opengl  vulkan"
    echo "  Special   : all   (builds every pipeline in the specified config)"
    echo "  Config    : debug  release  (default: debug)"
    echo "  -warnings : show compiler warnings (off by default — builds are quiet)"
    echo ""
    echo "Examples:"
    echo "  $(basename "$0") opengl"
    echo "  $(basename "$0") vulkan release"
    echo "  $(basename "$0") all debug"
    echo "  $(basename "$0") all release"
    echo "  $(basename "$0") opengl -warnings"
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
# The per-renderer install is skipped here (SKIP_INSTALL, same pattern as
# cmake-build.bat) and runs once at the end, only if every pipeline passed.
if [[ "${ARG1,,}" == "all" ]]; then
    ALL_CONFIG="debug"
    [[ "${ARG2,,}" == "release" ]] && ALL_CONFIG="release"
    ALL_CONFIG_DIR="Debug"
    [[ "${ALL_CONFIG}" == "release" ]] && ALL_CONFIG_DIR="Release"

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
            if SKIP_INSTALL=1 "${BASH_SOURCE[0]}" "${renderer}" "${ALL_CONFIG}" ${CLEAN_BUILD:+-clean} ${SHOW_WARNINGS:+-warnings}; then
                ALL_PASS_LIST="${ALL_PASS_LIST} ${renderer}"
            else
                ALL_FAIL_COUNT=$((ALL_FAIL_COUNT + 1))
                ALL_FAIL_LIST="${ALL_FAIL_LIST} ${renderer}"
            fi
            IS_FIRST_BUILD=0
        else
            if SKIP_INSTALL=1 SKIP_VERSION_INCREMENT=1 "${BASH_SOURCE[0]}" "${renderer}" "${ALL_CONFIG}" ${CLEAN_BUILD:+-clean} ${SHOW_WARNINGS:+-warnings}; then
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
    echo "Installing built executables to the project root..."
    install_to_root "OpenGL" "${ALL_CONFIG_DIR}" || exit 1
    install_to_root "Vulkan" "${ALL_CONFIG_DIR}" || exit 1
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
    -DCMAKE_BUILD_TYPE="${CONFIG}" \
    "${CMAKE_CXX_FLAGS_ARG}"

"${CMAKE_EXE}" --build "${BUILD_DIR}" --parallel "$(nproc 2>/dev/null || echo 4)"

echo ""
echo "Build succeeded: ${RENDERER} ${CONFIG} -- $(date)"

# --- Install to project root (skipped when called from 'all') ---
if [[ -z "${SKIP_INSTALL:-}" ]]; then
    install_to_root "${RENDERER}" "${CONFIG}" || exit 1
fi
