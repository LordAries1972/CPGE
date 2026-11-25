#!/usr/bin/env bash
# reset.sh — reset the game to a fresh start: delete the high-score table
# and clear out its recordings. (25 November 2026, Daniel.)
#
#   ./reset.sh [assets-dir]
#
# Deletes exactly two things, both inside the Assets tree:
#   1. <assets>/highscores.txt — the MyHighScores table
#      (Docs/Vectors-Example-Usage.md: the table is saved as
#      `S <game> <score> <wave> <name> ...` / `W <game> <bestWave>` lines
#      with a CPGE_HIGHSCORES_2 header; the engine binds it with
#      scores.SetFile(assetsDir / "highscores.txt")).
#   2. The recording files in <assets>/Playbacks/ — the videos the table
#      references: <id>.mp4, the <id>.json manifest sidecar, and the dated
#      object-state files <scene>-<YYYY-MM-DD_HH-MM-SS>-<id>.cpr
#      (Vectors.cpp: MyHighScores::RecordingPath / DiscardRecording /
#      PruneRecordings). Only those three patterns are removed; anything
#      else in Playbacks/ is left alone, exactly as the engine's own
#      PruneRecordings() never touches unrelated files.
#
# The Playbacks/ FOLDER itself is kept, and the recordings are deleted
# by contents only: nothing in the engine creates Playbacks/ on demand
# (no create_directories call for it anywhere; the FFmpeg avio_open and
# the MediaCodec fopen writers both fail when the parent directory does
# not exist), so removing the folder would break the next recording.
# Manual screen captures (Assets/recording_YYYYMMDD_HHMMSS.mp4, written
# by main.cpp) are NOT table recordings and are NOT deleted.
#
# The assets dir defaults to the repo's Assets/ (found from this script's
# own location, never from the caller's working directory). A different
# assets dir may be passed as the first argument — it must be a folder
# named "Assets", or the script refuses to touch it. Nothing outside an
# Assets tree is ever deleted.
#
# Idempotent: succeeds quietly (exit 0) when there is nothing to remove.
# Prints what it removed. Exit 1 on any failure — the build scripts call
# this on clean release builds and treat a failed reset as a build
# failure, so a release never ships with stale scores.
#
# Also runs under Git Bash on Windows (same POSIX shell).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# --- Resolve the assets dir ----------------------------------------------
ASSETS_DIR="${1:-${PROJECT_ROOT}/Assets}"
# A relative argument is resolved against the caller's working directory
# (the caller's explicit choice); the default is anchored to this script.
case "${ASSETS_DIR}" in
    /*) ;;
    *)  ASSETS_DIR="$(pwd)/${ASSETS_DIR}" ;;
esac
# Strip a trailing slash so the leaf-name guard below sees "Assets".
ASSETS_DIR="${ASSETS_DIR%/}"

# Guard: the target must be a folder named "Assets" — never delete
# anything outside an assets tree (a typo'd path must fail here, not
# delete the wrong folder).
case "${ASSETS_DIR}" in
    */Assets) ;;
    *)
        echo "ERROR: reset target is not an Assets folder: ${ASSETS_DIR}"
        echo "  Pass the Assets folder explicitly, e.g.: reset.sh /path/to/CPGE/Assets"
        exit 1
        ;;
esac

if [[ ! -d "${ASSETS_DIR}" ]]; then
    echo "Reset: ${ASSETS_DIR} not found — nothing to reset."
    exit 0
fi

REMOVED_TABLE=0
REMOVED_REC=0

# --- 1. The high-score table ----------------------------------------------
TABLE_FILE="${ASSETS_DIR}/highscores.txt"
if [[ -e "${TABLE_FILE}" ]]; then
    if [[ ! -f "${TABLE_FILE}" ]]; then
        echo "ERROR: ${TABLE_FILE} exists but is not a regular file — refusing to remove it."
        exit 1
    fi
    rm -f "${TABLE_FILE}" || { echo "ERROR: could not remove ${TABLE_FILE}"; exit 1; }
    REMOVED_TABLE=1
    echo "Reset: removed ${TABLE_FILE}"
fi

# --- 2. The table's recordings --------------------------------------------
# Contents only, engine patterns only (*.mp4 video, *.json manifest,
# *.cpr state file), no recursion: the engine never creates
# subdirectories in Playbacks/, and unrelated files are never touched.
# The Playbacks/ folder itself stays — nothing recreates it.
PLAYBACKS_DIR="${ASSETS_DIR}/Playbacks"
if [[ -d "${PLAYBACKS_DIR}" ]]; then
    shopt -s nullglob
    for _f in "${PLAYBACKS_DIR}"/*.mp4 "${PLAYBACKS_DIR}"/*.json "${PLAYBACKS_DIR}"/*.cpr; do
        [[ -f "$_f" ]] || continue
        rm -f "$_f" || { echo "ERROR: could not remove ${_f}"; exit 1; }
        REMOVED_REC=$((REMOVED_REC + 1))
    done
    shopt -u nullglob
    if [[ "${REMOVED_REC}" -gt 0 ]]; then
        echo "Reset: removed ${REMOVED_REC} recording file(s) from ${PLAYBACKS_DIR}/"
    fi
fi

if [[ "${REMOVED_TABLE}" -eq 0 && "${REMOVED_REC}" -eq 0 ]]; then
    echo "Reset: nothing to remove — high scores already fresh."
fi

exit 0
