@echo off
setlocal

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

set CMAKE_EXE=D:\Programs\CMake\bin\cmake.exe

if /i "%CONFIG%"=="clean" (
    echo Cleaning all build directories and compiled artifacts...
    if exist "build"              rmdir /s /q "build"
    if exist "x64"               rmdir /s /q "x64"
    if exist "CrossPla.2bd3f178" rmdir /s /q "CrossPla.2bd3f178"
    for %%F in (*.pdb *.ilk *.obj *.pch *.idb) do (
        if exist "%%F" del /q "%%F"
    )
    echo Clean complete.
    goto :end
)

:: Release any file handles left by a previous cancelled or failed build
:: before we start a new one. Ctrl+C cannot be trapped in batch so this
:: "kill at start" is the primary guarantee.
call :killcompiler

set BUILD_DIR=build\%CONFIG%

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

"%CMAKE_EXE%" -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto :error

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 goto :error

echo.
echo Build succeeded: %CONFIG% -- %DATE%  %TIME%
goto :end

:error
:: Release handles from the build that just failed or was cancelled
call :killcompiler
echo.
echo Build FAILED: %CONFIG% -- %DATE%  %TIME%
exit /b 1

:end
endlocal
exit /b 0

:: -----------------------------------------------------------------------
:: Kill all cl.exe and mspdbsrv.exe processes to release any locked handles.
:: Errors are suppressed — if nothing is running, taskkill exits non-zero
:: but that is harmless.
:: -----------------------------------------------------------------------
:killcompiler
taskkill /F /IM cl.exe       >nul 2>&1
taskkill /F /IM mspdbsrv.exe >nul 2>&1
exit /b 0
