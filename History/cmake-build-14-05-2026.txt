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
echo.
echo Build FAILED: %CONFIG% -- %DATE%  %TIME%
exit /b 1

:end
endlocal
