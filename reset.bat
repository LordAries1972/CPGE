@echo off
:: reset.bat — reset the game to a fresh start: delete the high-score table
:: and clear out its recordings. (25 November 2026, Daniel.)
::
::   reset.bat [assets-dir]
::
:: Deletes exactly two things, both inside the Assets tree:
::   1. <assets>\highscores.txt — the MyHighScores table
::      (Docs/Vectors-Example-Usage.md: the table is saved as
::      `S <game> <score> <wave> <name> ...` / `W <game> <bestWave>` lines
::      with a CPGE_HIGHSCORES_2 header; the engine binds it with
::      scores.SetFile(assetsDir / "highscores.txt")).
::   2. The recording files in <assets>\Playbacks\ — the videos the table
::      references: <id>.mp4, the <id>.json manifest sidecar, and the dated
::      object-state files <scene>-<YYYY-MM-DD_HH-MM-SS>-<id>.cpr
::      (Vectors.cpp: MyHighScores::RecordingPath / DiscardRecording /
::      PruneRecordings). Only those three patterns are removed; anything
::      else in Playbacks\ is left alone, exactly as the engine's own
::      PruneRecordings() never touches unrelated files.
::
:: The Playbacks\ FOLDER itself is kept, and the recordings are deleted
:: by contents only: nothing in the engine creates Playbacks\ on demand
:: (no create_directories call for it anywhere; the FFmpeg avio_open and
:: the MediaCodec fopen writers both fail when the parent directory does
:: not exist), so removing the folder would break the next recording.
:: Manual screen captures (Assets\recording_YYYYMMDD_HHMMSS.mp4, written
:: by main.cpp) are NOT table recordings and are NOT deleted.
::
:: The assets dir defaults to the repo's Assets\ (found from this script's
:: own location, %~dp0, never from the caller's working directory). A
:: different assets dir may be passed as the first argument — it must be
:: a folder named "Assets", or the script refuses to touch it. Nothing
:: outside an Assets tree is ever deleted.
::
:: Idempotent: succeeds quietly (exit 0) when there is nothing to remove.
:: Prints what it removed. Exit 1 on any failure — cmake-build.bat calls
:: this on clean release builds and treats a failed reset as a build
:: failure, so a release never ships with stale scores.
setlocal enabledelayedexpansion

:: Anchor all file operations to the directory containing this script so
:: the reset works correctly regardless of the caller's working directory.
set "SCRIPT_DIR=%~dp0"
set "ASSETS_DIR=%SCRIPT_DIR%Assets"
if not "%~1"=="" set "ASSETS_DIR=%~1"

:: Strip a trailing backslash (a caller-supplied path may have one) so
:: the leaf-name guard below sees "Assets".
if "%ASSETS_DIR:~-1%"=="\" set "ASSETS_DIR=%ASSETS_DIR:~0,-1%"

:: Guard: the target must be a folder named "Assets" — never delete
:: anything outside an assets tree (a typo'd path must fail here, not
:: delete the wrong folder).
for %%I in ("%ASSETS_DIR%") do set "LEAF=%%~nxI"
if /i not "%LEAF%"=="Assets" (
    echo ERROR: reset target is not an Assets folder: "%ASSETS_DIR%"
    echo   Pass the Assets folder explicitly, e.g. reset.bat "C:\CPGE\Assets"
    endlocal & exit /b 1
)

if not exist "%ASSETS_DIR%\" (
    echo Reset: "%ASSETS_DIR%" not found -- nothing to reset.
    endlocal & exit /b 0
)

set "REMOVED_TABLE=0"
set "REMOVED_REC=0"

:: --- 1. The high-score table ---
if exist "%ASSETS_DIR%\highscores.txt" (
    del /q "%ASSETS_DIR%\highscores.txt"
    if errorlevel 1 (
        echo ERROR: could not remove "%ASSETS_DIR%\highscores.txt"
        endlocal & exit /b 1
    )
    set "REMOVED_TABLE=1"
    echo Reset: removed "%ASSETS_DIR%\highscores.txt"
)

:: --- 2. The table's recordings ---
:: Contents only, engine patterns only (*.mp4 video, *.json manifest,
:: *.cpr state file), no recursion: the engine never creates
:: subdirectories in Playbacks\, and unrelated files are never touched.
:: The Playbacks\ folder itself stays — nothing recreates it.
:: (A for-loop over a wildcard that matches nothing runs once with the
:: literal pattern, so each candidate is checked with if exist first.)
if exist "%ASSETS_DIR%\Playbacks\" (
    for %%F in ("%ASSETS_DIR%\Playbacks\*.mp4" "%ASSETS_DIR%\Playbacks\*.json" "%ASSETS_DIR%\Playbacks\*.cpr") do (
        if exist "%%F" (
            del /q "%%F"
            if errorlevel 1 (
                echo ERROR: could not remove "%%F"
                endlocal & exit /b 1
            )
            set /a REMOVED_REC+=1
        )
    )
    if !REMOVED_REC! GTR 0 (
        echo Reset: removed !REMOVED_REC! recording file^(s^) from "%ASSETS_DIR%\Playbacks\"
    )
)

if "%REMOVED_TABLE%"=="0" if "%REMOVED_REC%"=="0" (
    echo Reset: nothing to remove -- high scores already fresh.
)

endlocal & exit /b 0
