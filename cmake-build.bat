@echo off
setlocal

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

set BUILD_DIR=build\%CONFIG%
set CMAKE_EXE=D:\Programs\CMake\bin\cmake.exe

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
