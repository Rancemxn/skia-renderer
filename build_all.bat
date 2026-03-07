@echo off
setlocal enabledelayedexpansion

REM ========================================
REM Skia Renderer - Complete Build (One-Click)
REM ========================================

set "SCRIPT_DIR=%~dp0"

REM Default options
set "SKIP_SYNC=0"
set "SKIP_DEPS=0"
set "CLEAN=0"
set "BUILD_TYPE=Release"
set "USE_MIRROR=0"
set "PROXY="
set "USE_LLVM=1"
set "LLVM_PATH="

REM ========================================
REM Parse Arguments
REM ========================================

:parse_args
if "%~1"=="" goto :end_parse

if /i "%~1"=="--skip-sync" set "SKIP_SYNC=1"
if /i "%~1"=="--skip-deps" set "SKIP_DEPS=1"
if /i "%~1"=="--clean" set "CLEAN=1"
if /i "%~1"=="--mirror" set "USE_MIRROR=1"
if /i "%~1"=="--build-type" ( set "BUILD_TYPE=%~2" & shift )
if /i "%~1"=="--proxy" ( set "PROXY=%~2" & shift )
if /i "%~1"=="--llvm" (
    set "USE_LLVM=1"
    if not "%~2"=="" (
        if not "%~2:~0,2%"=="--" (
            set "LLVM_PATH=%~2"
            shift
        )
    )
)
if /i "%~1"=="--vs" set "USE_LLVM=0"

if /i "%~1"=="--help" (
    echo ========================================
    echo Skia Renderer - Complete Build
    echo ========================================
    echo.
    echo Usage: %~nx0 [options]
    echo.
    echo Compiler Options:
    echo   --llvm [PATH]    Use LLVM/Clang + Ninja (default)
    echo   --vs             Use Visual Studio instead
    echo.
    echo Build Options:
    echo   --skip-sync      Skip dependency download
    echo   --skip-deps      Skip dependency build
    echo   --clean          Clean before building
    echo   --build-type     Release or Debug (default: Release)
    echo.
    echo Download Options:
    echo   --mirror         Use Chinese mirrors
    echo   --proxy URL      Use proxy for downloads
    echo.
    echo Prerequisites:
    echo   - LLVM/Clang + Ninja OR Visual Studio 2022
    echo   - CMake 3.20+
    echo   - aria2 (winget install aria2)
    echo   - 7-Zip (winget install 7zip.7zip)
    echo   - Python 3.8+
    echo   - Git
    echo   - Vulkan SDK (set VULKAN_SDK env var)
    exit /b 0
)

shift
goto :parse_args
:end_parse

echo ========================================
echo Skia Renderer - Complete Build
echo ========================================
echo.
echo Options:
echo   Build Type: %BUILD_TYPE%
echo   Clean: %CLEAN%
echo   Use LLVM: %USE_LLVM%
if defined LLVM_PATH echo   LLVM Path: %LLVM_PATH%
echo   Use Mirror: %USE_MIRROR%
if defined PROXY echo   Proxy: %PROXY%
echo.

REM ========================================
REM Step 1: Sync Dependencies
REM ========================================

if "%SKIP_SYNC%"=="1" (
    echo [SKIP] Step 1: Sync dependencies
    goto :step2
)

echo ========================================
echo [Step 1/3] Syncing dependencies...
echo ========================================
echo.

set "SYNC_ARGS="
if "%USE_MIRROR%"=="1" set "SYNC_ARGS=%SYNC_ARGS% --mirror"
if defined PROXY set "SYNC_ARGS=%SYNC_ARGS% --proxy %PROXY%"

call "%SCRIPT_DIR%sync_deps.bat" %SYNC_ARGS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Dependency sync failed
    exit /b 1
)

:step2

REM ========================================
REM Step 2: Build Dependencies
REM ========================================

if "%SKIP_DEPS%"=="1" (
    echo [SKIP] Step 2: Build dependencies
    goto :step3
)

echo ========================================
echo [Step 2/3] Building dependencies...
echo ========================================
echo.

set "DEPS_ARGS=--build-type %BUILD_TYPE%"
if "%CLEAN%"=="1" set "DEPS_ARGS=%DEPS_ARGS% --clean"
if "%USE_LLVM%"=="1" (
    set "DEPS_ARGS=%DEPS_ARGS% --llvm"
    if defined LLVM_PATH set "DEPS_ARGS=%DEPS_ARGS% %LLVM_PATH%"
)

call "%SCRIPT_DIR%build_deps.bat" %DEPS_ARGS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Dependency build failed
    exit /b 1
)

:step3

REM ========================================
REM Step 3: Build Main Project
REM ========================================

echo ========================================
echo [Step 3/3] Building main project...
echo ========================================
echo.

set "BUILD_ARGS=--build-type %BUILD_TYPE%"
if "%CLEAN%"=="1" set "BUILD_ARGS=%BUILD_ARGS% --clean"
if "%USE_LLVM%"=="1" (
    set "BUILD_ARGS=%BUILD_ARGS% --llvm"
    if defined LLVM_PATH set "BUILD_ARGS=%BUILD_ARGS% %LLVM_PATH%"
)

call "%SCRIPT_DIR%build_windows.bat" %BUILD_ARGS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Main project build failed
    exit /b 1
)

REM ========================================
REM Done
REM ========================================

echo ========================================
echo Build Complete!
echo ========================================
echo.

if "%USE_LLVM%"=="1" (
    echo Run: build\skia-renderer.exe
) else (
    echo Run: build\%BUILD_TYPE%\skia-renderer.exe
)

echo.

endlocal
