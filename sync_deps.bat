@echo off
setlocal enabledelayedexpansion

REM ========================================
REM Skia Renderer - Dependency Sync Script
REM Uses aria2 for fast parallel downloads
REM ========================================

set "DEPS_DIR=%~dp0deps"
set "DOWNLOADS_DIR=%~dp0downloads"

REM Parse arguments
set "SKIP_SKIA=0"
set "SKIP_SDL=0"
set "SKIP_VKBOOTSTRAP=0"
set "SKIP_VMA=0"
set "USE_MIRROR=0"
set "PROXY="

:parse_args
if "%~1"=="" goto :end_parse
if /i "%~1"=="--skip-skia" set "SKIP_SKIA=1"
if /i "%~1"=="--skip-sdl" set "SKIP_SDL=1"
if /i "%~1"=="--skip-vkbootstrap" set "SKIP_VKBOOTSTRAP=1"
if /i "%~1"=="--skip-vma" set "SKIP_VMA=1"
if /i "%~1"=="--mirror" set "USE_MIRROR=1"
if /i "%~1"=="--proxy" (
    set "PROXY=%~2"
    shift
)
if /i "%~1"=="--help" (
    echo Usage: %~nx0 [options]
    echo.
    echo Options:
    echo   --skip-skia        Skip Skia download
    echo   --skip-sdl         Skip SDL3 download
    echo   --skip-vkbootstrap Skip vk-bootstrap download
    echo   --skip-vma         Skip VMA download
    echo   --mirror           Use Chinese mirrors for faster download
    echo   --proxy URL        Use proxy (e.g., http://127.0.0.1:7890)
    echo   --help             Show this help
    exit /b 0
)
shift
goto :parse_args
:end_parse

echo ========================================
echo Skia Renderer - Dependency Sync
echo ========================================
echo.

REM Check for aria2
where aria2c >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: aria2c not found in PATH.
    echo Please install aria2: winget install aria2
    echo Or download from: https://github.com/aria2/aria2/releases
    exit /b 1
)

REM Create directories
if not exist "%DEPS_DIR%" mkdir "%DEPS_DIR%"
if not exist "%DOWNLOADS_DIR%" mkdir "%DOWNLOADS_DIR%"

REM Set aria2 options
set "ARIA2_OPTS=-x 16 -s 32 -k 1M --check-certificate=false --file-allocation=none"
if defined PROXY (
    set "ARIA2_OPTS=%ARIA2_OPTS% --all-proxy=%PROXY%"
)

echo aria2 options: %ARIA2_OPTS%
echo.

REM ========================================
REM SDL3
REM ========================================
if "%SKIP_SDL%"=="1" (
    echo [SKIP] SDL3
    goto :sdl_done
)

echo [1/4] Downloading SDL3...
set "SDL_VERSION=3.4.2"
set "SDL_URL=https://github.com/libsdl-org/SDL/releases/download/release-%SDL_VERSION%/SDL3-devel-%SDL_VERSION%-VC.zip"
set "SDL_MIRROR_URL=https://ghp.ci/https://github.com/libsdl-org/SDL/releases/download/release-%SDL_VERSION%/SDL3-devel-%SDL_VERSION%-VC.zip"

if "%USE_MIRROR%"=="1" (
    aria2c %ARIA2_OPTS% -d "%DOWNLOADS_DIR%" -o "SDL3-devel-%SDL_VERSION%-VC.zip" "%SDL_MIRROR_URL%"
) else (
    aria2c %ARIA2_OPTS% -d "%DOWNLOADS_DIR%" -o "SDL3-devel-%SDL_VERSION%-VC.zip" "%SDL_URL%"
)

if exist "%DOWNLOADS_DIR%\SDL3-devel-%SDL_VERSION%-VC.zip" (
    echo Extracting SDL3...
    powershell -Command "Expand-Archive -Path '%DOWNLOADS_DIR%\SDL3-devel-%SDL_VERSION%-VC.zip' -DestinationPath '%DEPS_DIR%' -Force"
    if exist "%DEPS_DIR%\SDL3-%SDL_VERSION%" (
        rename "%DEPS_DIR%\SDL3-%SDL_VERSION%" "SDL3"
    )
    echo SDL3 done!
) else (
    echo ERROR: Failed to download SDL3
)

:sdl_done
echo.

REM ========================================
REM vk-bootstrap
REM ========================================
if "%SKIP_VKBOOTSTRAP%"=="1" (
    echo [SKIP] vk-bootstrap
    goto :vkbootstrap_done
)

echo [2/4] Downloading vk-bootstrap...
set "VKBOOTSTRAP_VERSION=1.4.343"
set "VKBOOTSTRAP_URL=https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v%VKBOOTSTRAP_VERSION%.zip"
set "VKBOOTSTRAP_MIRROR_URL=https://ghp.ci/https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v%VKBOOTSTRAP_VERSION%.zip"

if "%USE_MIRROR%"=="1" (
    aria2c %ARIA2_OPTS% -d "%DOWNLOADS_DIR%" -o "vk-bootstrap-%VKBOOTSTRAP_VERSION%.zip" "%VKBOOTSTRAP_MIRROR_URL%"
) else (
    aria2c %ARIA2_OPTS% -d "%DOWNLOADS_DIR%" -o "vk-bootstrap-%VKBOOTSTRAP_VERSION%.zip" "%VKBOOTSTRAP_URL%"
)

if exist "%DOWNLOADS_DIR%\vk-bootstrap-%VKBOOTSTRAP_VERSION%.zip" (
    echo Extracting vk-bootstrap...
    powershell -Command "Expand-Archive -Path '%DOWNLOADS_DIR%\vk-bootstrap-%VKBOOTSTRAP_VERSION%.zip' -DestinationPath '%DEPS_DIR%' -Force"
    if exist "%DEPS_DIR%\vk-bootstrap-%VKBOOTSTRAP_VERSION%" (
        rename "%DEPS_DIR%\vk-bootstrap-%VKBOOTSTRAP_VERSION%" "vk-bootstrap"
    )
    echo vk-bootstrap done!
) else (
    echo ERROR: Failed to download vk-bootstrap
)

:vkbootstrap_done
echo.

REM ========================================
REM VulkanMemoryAllocator
REM ========================================
if "%SKIP_VMA%"=="1" (
    echo [SKIP] VulkanMemoryAllocator
    goto :vma_done
)

echo [3/4] Downloading VulkanMemoryAllocator...
set "VMA_VERSION=3.3.0"
set "VMA_URL=https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v%VMA_VERSION%.zip"
set "VMA_MIRROR_URL=https://ghp.ci/https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v%VMA_VERSION%.zip"

if "%USE_MIRROR%"=="1" (
    aria2c %ARIA2_OPTS% -d "%DOWNLOADS_DIR%" -o "VMA-%VMA_VERSION%.zip" "%VMA_MIRROR_URL%"
) else (
    aria2c %ARIA2_OPTS% -d "%DOWNLOADS_DIR%" -o "VMA-%VMA_VERSION%.zip" "%VMA_URL%"
)

if exist "%DOWNLOADS_DIR%\VMA-%VMA_VERSION%.zip" (
    echo Extracting VulkanMemoryAllocator...
    powershell -Command "Expand-Archive -Path '%DOWNLOADS_DIR%\VMA-%VMA_VERSION%.zip' -DestinationPath '%DEPS_DIR%' -Force"
    if exist "%DEPS_DIR%\VulkanMemoryAllocator-%VMA_VERSION%" (
        rename "%DEPS_DIR%\VulkanMemoryAllocator-%VMA_VERSION%" "VulkanMemoryAllocator"
    )
    echo VulkanMemoryAllocator done!
) else (
    echo ERROR: Failed to download VulkanMemoryAllocator
)

:vma_done
echo.

REM ========================================
REM Skia
REM ========================================
if "%SKIP_SKIA%"=="1" (
    echo [SKIP] Skia
    goto :skia_done
)

echo [4/4] Downloading Skia...
echo This requires git and depot_tools.
echo.

REM Check for git
where git >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: git not found. Please install git.
    goto :skia_done
)

REM Check for depot_tools
if not defined DEPOT_TOOLS (
    echo depot_tools not found. Downloading...
    if "%USE_MIRROR%"=="1" (
        set "DEPOT_URL=https://ghp.ci/https://chromium.googlesource.com/chromium/tools/depot_tools.git"
    ) else (
        set "DEPOT_URL=https://chromium.googlesource.com/chromium/tools/depot_tools.git"
    )
    git clone --depth 1 "%DEPOT_URL%" "%DEPS_DIR%\depot_tools"
    set "DEPOT_TOOLS=%DEPS_DIR%\depot_tools"
    set "PATH=%DEPOT_TOOLS%;%PATH%"
) else (
    echo Using depot_tools from: %DEPOT_TOOLS%
    set "PATH=%DEPOT_TOOLS%;%PATH%"
)

if not exist "%DEPS_DIR%\skia" (
    echo Cloning Skia...
    if "%USE_MIRROR%"=="1" (
        REM Use mirror for faster clone in China
        git clone --depth 1 https://ghp.ci/https://skia.googlesource.com/skia.git "%DEPS_DIR%\skia"
    ) else (
        git clone --depth 1 https://skia.googlesource.com/skia.git "%DEPS_DIR%\skia"
    )
    
    cd /d "%DEPS_DIR%\skia"
    
    REM Checkout specific branch if needed
    REM git checkout chrome/m146
    
    echo Syncing Skia dependencies...
    python tools/git-sync-deps
) else (
    echo Skia directory already exists. Skipping clone.
    echo Run 'rmdir /s /q deps\skia' to force re-download.
)

:skia_done
echo.

REM ========================================
REM Summary
REM ========================================
echo ========================================
echo Dependency Sync Complete!
echo ========================================
echo.
echo Downloaded dependencies:
dir /b "%DEPS_DIR%" 2>nul
echo.
echo Next steps:
echo   1. Run build_deps.bat to compile dependencies
echo   2. Run build_windows.bat to build the project
echo.

endlocal
