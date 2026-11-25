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
# Before building (debug and release alike) the script checks that every
# dependency the build needs is present and downloads any that are
# missing (apt on Linux; FFmpeg comes from the distro -dev packages,
# verified to satisfy the build). See check_linux_deps below.
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
  0. Checks every dependency the build needs (cmake, compiler, make,
     OpenGL / X11 / GLEW / GLU headers, and for Vulkan the Vulkan SDK
     + xcb + X11-xcb headers, plus shaderc, glslangValidator, ALSA,
     FFmpeg and eSpeak-ng) and downloads any that are missing — apt on
     Linux (FFmpeg from the distro -dev packages). This runs
     before building, for debug and release alike. A required
     dependency that cannot be downloaded stops the build here with
     the exact fix; an optional one only warns (CMake disables that
     feature). When everything is present this step is a silent no-op.
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

# --- Dependency check: download if not found (25 November 2026) -------------
# Before building (and therefore before packaging, for both debug and
# release — this check is NOT gated on CONFIG), every dependency the
# build needs is verified present, and downloaded when it is missing.
# This is the "repair" step: a VM reset wipes the installed -dev
# packages, and a build that fails halfway through on a missing header
# is an environment failure, not a code failure — the check repairs the
# environment first so the build can fulfil.
#
# HARD dependencies (the build cannot succeed without them): cmake, a C++
# compiler, make, and the headers CMakeLists.txt requires — OpenGL, X11,
# GLEW, GLU, and for the Vulkan renderer Vulkan + xcb + X11-xcb. If one
# is missing and cannot be downloaded, the script prints the exact fix
# and exits non-zero BEFORE building.
# SOFT dependencies (CMakeLists.txt treats them as optional — the build
# still succeeds with that feature disabled): shaderc + glslangValidator
# (Vulkan only), ALSA, FFmpeg, eSpeak-ng. A missing one is still
# downloaded when possible; if the download fails the script prints a
# WARNING with the exact fix and builds on (CMake disables the feature).
# GLM and miniaudio are VENDORED in repo/include/ and are never
# downloaded — CMakeLists.txt already fails fast with a clear message
# if include/glm/glm.hpp is missing.
#
# Linux system packages come from apt (sudo when not root). If apt
# cannot reach the network, the cached .debs in /var/cache/apt/archives
# are tried with dpkg as a fallback. FFmpeg comes from the distro -dev
# packages too (Ubuntu 24.04: FFmpeg 6.1.1) — verified 25 November 2026
# that the engine compiles and links against them (the manual
# FFmpeg 8.1.2 headers in /usr/local/include are no longer needed;
# the old 8.x runtime libraries stay installed but unused once the
# 6.1 -dev packages provide the .so symlinks).
# When everything is present the check is a silent no-op and the build
# proceeds exactly as before.
_have_cmd() { command -v "$1" &>/dev/null; }   # _have_cmd <name>
_have_file() { [[ -f "$1" ]]; }                # _have_file <path>

# apt helpers. _apt_sudo is set by _apt_init: empty when root, "sudo -n"
# when passwordless sudo works, unset when neither is available (then
# _apt_install fails fast with the manual command instead of hanging on
# a password prompt).
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

# _apt_update_once — refresh the package lists once per script run
# (and once per `all` run: the flag is exported so a re-invoked child
# skips the second update). Only called when an install is actually
# needed, so the no-op path never pays for it. A failed update is not
# fatal here — the cached .debs may still satisfy the install.
# No update is needed when package lists already exist: apt writes
# *_Packages into place only when complete, so existing lists are
# usable even if they are a day old (and an update can take ten
# minutes on a slow mirror).
_apt_update_once() {
    [[ -n "${CPGE_APT_UPDATED:-}" ]] && return 0
    if ls /var/lib/apt/lists/*_Packages >/dev/null 2>&1; then
        export CPGE_APT_UPDATED=1
        return 0
    fi
    echo "Dependency check: refreshing package lists (apt-get update — can take a while on a slow mirror)..."
    if ${_APT_SUDO} apt-get update; then
        export CPGE_APT_UPDATED=1
    else
        echo "NOTE: apt-get update failed — continuing with the cached package lists."
    fi
    return 0
}

# _apt_install_pkgs <pkg...> — install the named packages with apt, with
# a dpkg fallback from the apt cache. Returns 0 only if every package is
# verifiably installed afterwards (checked by the caller's presence
# test, not by apt's exit code alone).
_apt_install_pkgs() {
    local _pkgs=("$@") _p _f
    _apt_init
    if [[ "${_APT_SUDO}" == "__NONE__" ]] || ! _have_cmd apt-get; then
        echo "ERROR: cannot install packages — no usable apt (need root or passwordless sudo)."
        echo "  Run this by hand, then re-run the build:"
        echo "    sudo apt-get install -y ${_pkgs[*]}"
        return 1
    fi
    _apt_update_once
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

# _apt_fix_line <pkg...> — the one-liner the user runs by hand.
_apt_fix_line() { echo "    sudo apt-get install -y $*"; }

# FFmpeg (soft): headers in /usr/include, the multiarch dir
# /usr/include/x86_64-linux-gnu (where Ubuntu's -dev packages put
# them), or /usr/local/include, and libraries in the loader path.
# Returns 0 when FFmpeg is usable, 1 when it is not (the caller warns
# and continues — CMake disables MoviePlayer / ScreenRecorder when
# FFmpeg is absent).
_ffmpeg_present() {
    local _h _l
    for _h in /usr/include/libavformat/avformat.h /usr/include/x86_64-linux-gnu/libavformat/avformat.h /usr/local/include/libavformat/avformat.h; do
        [[ -f "$_h" ]] || continue
        for _l in /usr/lib/x86_64-linux-gnu/libavformat.so* /usr/local/lib/x86_64-linux-gnu/libavformat.so*; do
            [[ -e "$_l" ]] && return 0
        done
        # Headers found but no .so in the usual places — ask the loader.
        ldconfig -p 2>/dev/null | grep -q "libavformat.so" && return 0
        return 1
    done
    return 1
}

# The Linux dependency check. RENDERER must be set before calling.
# Exits 1 (via return 1 — the caller exits) when a HARD dependency is
# missing and could not be downloaded; soft failures only warn.
check_linux_deps() {
    local _missing_hard=() _missing_soft=() _p _vulkan_sdk_inc=""
    # --- Tools (hard) ---
    if ! _have_cmd "${CMAKE_EXE}"; then
        echo "ERROR: cmake not found — it is required to configure the build."
        echo "  Install it with:"
        echo "    sudo apt install cmake          # Ubuntu/Debian"
        echo "    sudo dnf install cmake          # Fedora/RHEL"
        echo "    sudo pacman -S cmake            # Arch"
        echo "  Or set CMAKE_EXE to the full path of your cmake binary."
        return 1
    fi
    if ! _have_cmd g++ && ! _have_cmd clang++; then
        _missing_hard+=(g++)
    fi
    _have_cmd make || _missing_hard+=(make)

    # --- Headers (hard) — one apt package per header ---
    _have_file /usr/include/GL/gl.h       || _missing_hard+=(libgl-dev)
    _have_file /usr/include/X11/Xlib.h    || _missing_hard+=(libx11-dev)
    _have_file /usr/include/GL/glew.h     || _missing_hard+=(libglew-dev)
    _have_file /usr/include/GL/glu.h      || _missing_hard+=(libglu1-mesa-dev)
    if [[ "${RENDERER}" == "Vulkan" ]]; then
        [[ -n "${VULKAN_SDK:-}" ]] && _vulkan_sdk_inc="${VULKAN_SDK}/include/vulkan/vulkan.h"
        if ! _have_file /usr/include/vulkan/vulkan.h && ! _have_file "${_vulkan_sdk_inc}"; then
            _missing_hard+=(libvulkan-dev)
        fi
        _have_file /usr/include/xcb/xcb.h        || _missing_hard+=(libxcb1-dev)
        _have_file /usr/include/X11/Xlib-xcb.h   || _missing_hard+=(libx11-xcb-dev)
        # --- Soft (Vulkan only) ---
        _have_file /usr/include/shaderc/shaderc.h || _missing_soft+=(libshaderc-dev)
        _have_cmd glslangValidator                || _missing_soft+=(glslang-tools)
    fi

    # --- Install the missing hard packages (one apt run) ---
    if [[ ${#_missing_hard[@]} -gt 0 ]]; then
        echo "Dependency check: missing (required): ${_missing_hard[*]}"
        _apt_install_pkgs "${_missing_hard[@]}" || true
        # Re-verify every hard dependency after the install attempt.
        local _still_missing=()
        for _p in "${_missing_hard[@]}"; do
            case "$_p" in
                g++)   _have_cmd g++ || _have_cmd clang++ || _still_missing+=("$_p") ;;
                make)  _have_cmd make || _still_missing+=("$_p") ;;
                libgl-dev)        _have_file /usr/include/GL/gl.h       || _still_missing+=("$_p") ;;
                libx11-dev)       _have_file /usr/include/X11/Xlib.h    || _still_missing+=("$_p") ;;
                libglew-dev)      _have_file /usr/include/GL/glew.h     || _still_missing+=("$_p") ;;
                libglu1-mesa-dev) _have_file /usr/include/GL/glu.h      || _still_missing+=("$_p") ;;
                libvulkan-dev)    _have_file /usr/include/vulkan/vulkan.h || _have_file "${_vulkan_sdk_inc}" || _still_missing+=("$_p") ;;
                libxcb1-dev)      _have_file /usr/include/xcb/xcb.h     || _still_missing+=("$_p") ;;
                libx11-xcb-dev)   _have_file /usr/include/X11/Xlib-xcb.h || _still_missing+=("$_p") ;;
            esac
        done
        if [[ ${#_still_missing[@]} -gt 0 ]]; then
            echo "ERROR: could not obtain required dependencies: ${_still_missing[*]}"
            echo "  Install them by hand, then re-run the build:"
            _apt_fix_line "${_still_missing[@]}"
            return 1
        fi
        echo "Dependency check: required dependencies installed."
    fi

    # --- Install the missing soft packages (one apt run; failure warns) ---
    if [[ ${#_missing_soft[@]} -gt 0 ]]; then
        echo "Dependency check: missing (optional): ${_missing_soft[*]} — installing..."
        _apt_install_pkgs "${_missing_soft[@]}" || true
        for _p in "${_missing_soft[@]}"; do
            case "$_p" in
                libshaderc-dev) _have_file /usr/include/shaderc/shaderc.h || echo "WARNING: libshaderc-dev still missing — runtime GLSL→SPIR-V compilation disabled; pre-compiled .spv files required. Fix: $(_apt_fix_line libshaderc-dev)" ;;
                glslang-tools)  _have_cmd glslangValidator || echo "WARNING: glslangValidator still missing — Vulkan shaders are copied for runtime shaderc compile instead. Fix: $(_apt_fix_line glslang-tools)" ;;
            esac
        done
    fi

    # --- Soft (all renderers): ALSA, eSpeak-ng, FFmpeg ---
    _have_file /usr/include/alsa/asoundlib.h || {
        echo "Dependency check: missing (optional): libasound2-dev — installing..."
        _apt_install_pkgs libasound2-dev || true
        _have_file /usr/include/alsa/asoundlib.h || echo "WARNING: libasound2-dev still missing — SoundManager may produce no audio on Linux. Fix: $(_apt_fix_line libasound2-dev)"
    }
    _have_file /usr/include/espeak-ng/speak_lib.h || {
        echo "Dependency check: missing (optional): libespeak-ng-dev — installing..."
        _apt_install_pkgs libespeak-ng-dev || true
        _have_file /usr/include/espeak-ng/speak_lib.h || echo "WARNING: libespeak-ng-dev still missing — TTSManager disabled on Linux. Fix: $(_apt_fix_line libespeak-ng-dev)"
    }
    if ! _ffmpeg_present; then
        echo "Dependency check: FFmpeg headers/libraries missing (optional) — installing..."
        _apt_install_pkgs libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev || true
        if _ffmpeg_present; then
            echo "Dependency check: FFmpeg restored."
        else
            echo "WARNING: FFmpeg still missing — MoviePlayer/ScreenRecorder disabled; MPTM external samples silent."
            echo "  Fix by hand, then re-run the build:"
            echo "    sudo apt-get install -y libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev"
        fi
    fi
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

# --- Dependency check (before building, for debug AND release) -----------
# Verifies every dependency the build needs and downloads any that are
# missing (see check_linux_deps above). A missing required dependency
# that cannot be downloaded stops the build here, before CMake runs.
check_linux_deps || exit 1

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
