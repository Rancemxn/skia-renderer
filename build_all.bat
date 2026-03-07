@echo off
setlocal enabledelayedexpansion

REM ========================================
REM Skia Renderer - Build All (One-Click)
REM This script runs all build steps in order
REM ========================================

set "SCRIPT_DIR=%~dp0"

echo ========================================
echo Skia Renderer - Complete Build
echo ========================================
echo.
echo This script will:
echo   1. Download all dependencies (sync_deps.bat)
echo   2. Build all dependencies (build_deps.bat)
echo   3. Build the main project (build_windows.bat)
echo.

REM Parse arguments
set "SKIP_SYNC=0"
set "SKIP_DEPS=0"
set "CLEAN=0"
set "BUILD_TYPE=Release"
set "USE_MIRROR=0"

:parse_args
if "%~1"=="" goto :end_parse
if /i "%~1"=="--skip-sync" set "SKIP_SYNC=1"
if /i "%~1"=="--skip-deps" set "SKIP_DEPS=1"
if /i "%~1"=="--clean" set "CLEAN=1"
if /i "%~1"=="--mirror" set "USE_MIRROR=1"
if /i "%~1"=="--build-type" (
    set "BUILD_TYPE=%~2"
    shift
)
if /i "%~1"=="--help" (
    echo Usage: %~nx0 [options]
    echo.
    echo Options:
    echo   --skip-sync    Skip dependency download step
    echo   --skip-deps    Skip dependency build step
    echo   --clean        Clean build directories before building
    echo   --mirror       Use Chinese mirrors for faster downloads
    echo   --build-type   Release or Debug (default: Release)
    echo   --help         Show this help
    exit /b 0
)
shift
goto :parse_args
:end_parse

REM Step 1: Sync dependencies
if "%SKIP_SYNC%"=="1" (
    echo [SKIP] Step 1: Download dependencies
    goto :step2
)

echo.
echo [Step 1/3] Downloading dependencies...
echo.

set "SYNC_ARGS="
if "%USE_MIRROR%"=="1" set "SYNC_ARGS=--mirror"

call "%SCRIPT_DIR%sync_deps.bat" %SYNC_ARGS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Dependency sync failed.
    exit /b 1
)

:step2
REM Step 2: Build dependencies
if "%SKIP_DEPS%"=="1" (
    echo [SKIP] Step 2: Build dependencies
    goto :step3
)

echo.
echo [Step 2/3] Building dependencies...
echo.

set "DEPS_ARGS=--build-type %BUILD_TYPE%"
if "%CLEAN%"=="1" set "DEPS_ARGS=%DEPS_ARGS% --clean"

call "%SCRIPT_DIR%build_deps.bat" %DEPS_ARGS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Dependency build failed.
    exit /b 1
)

:step3
REM Step 3: Build main project
echo.
echo [Step 3/3] Building main project...
echo.

set "BUILD_ARGS=--build-type %BUILD_TYPE%"
if "%CLEAN%"=="1" set "BUILD_ARGS=%BUILD_ARGS% --clean"

call "%SCRIPT_DIR%build_windows.bat" %BUILD_ARGS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Main project build failed.
    exit /b 1
)

echo.
echo ========================================
echo Build Complete!
echo ========================================
echo.
echo Run the application:
echo   build\%BUILD_TYPE%\skia-renderer.exe
echo.

endlocal
