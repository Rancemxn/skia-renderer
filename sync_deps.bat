@echo off
setlocal enabledelayedexpansion

REM ========================================
REM Skia Renderer - Dependency Sync Script
REM Uses aria2 + 7z + Python
REM ========================================

set "SCRIPT_DIR=%~dp0"
set "DEPS_DIR=%SCRIPT_DIR%deps"
set "DOWNLOADS_DIR=%SCRIPT_DIR%downloads"

REM Default options
set "USE_MIRROR=0"
set "PROXY="
set "FORCE_OVERWRITE=1"
set "SKIP_SKIA=0"
set "SKIP_SDL=0"
set "SKIP_VKBOOTSTRAP=0"
set "SKIP_VMA=0"
set "SKIP_SKIA_DEPS=0"
set "PYTHON_EXE="

REM ========================================
REM Parse Arguments
REM ========================================

:parse_args
if "%~1"=="" goto :end_parse
if /i "%~1"=="--skip-skia" set "SKIP_SKIA=1"
if /i "%~1"=="--skip-sdl" set "SKIP_SDL=1"
if /i "%~1"=="--skip-vkbootstrap" set "SKIP_VKBOOTSTRAP=1"
if /i "%~1"=="--skip-vma" set "SKIP_VMA=1"
if /i "%~1"=="--skip-skia-deps" set "SKIP_SKIA_DEPS=1"
if /i "%~1"=="--no-overwrite" set "FORCE_OVERWRITE=0"
if /i "%~1"=="--mirror" set "USE_MIRROR=1"
if /i "%~1"=="--proxy" (
    set "PROXY=%~2"
    shift
)
if /i "%~1"=="--python" (
    set "PYTHON_EXE=%~2"
    shift
)
if /i "%~1"=="--help" (
    goto :show_help
)
shift
goto :parse_args
:end_parse
goto :main

:show_help
echo ========================================
echo Skia Renderer - Dependency Sync
echo ========================================
echo.
echo Usage: %~nx0 [options]
echo.
echo Options:
echo   --skip-skia         Skip Skia download
echo   --skip-sdl          Skip SDL3 download
echo   --skip-vkbootstrap  Skip vk-bootstrap download
echo   --skip-vma          Skip VMA download
echo   --skip-skia-deps    Skip Skia dependencies sync (use if it fails)
echo   --no-overwrite      Don't overwrite existing files
echo   --mirror            Use Chinese mirrors
echo   --proxy URL         Use proxy (e.g., http://127.0.0.1:7890)
echo   --python PATH       Python executable path
echo   --help              Show this help
exit /b 0

:main

echo ========================================
echo Skia Renderer - Dependency Sync
echo ========================================
echo.

REM ========================================
REM Check Tools
REM ========================================

echo [Checking Tools]
echo.

REM Check aria2
where aria2c >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: aria2c not found in PATH.
    echo Install: winget install aria2
    echo Or download: https://github.com/aria2/aria2/releases
    exit /b 1
)
echo [OK] aria2c found

REM Check 7z
where 7z >nul 2>&1
if %ERRORLEVEL% neq 0 (
    REM Try common paths
    if exist "C:\Program Files\7-Zip\7z.exe" (
        set "SEVENZIP=C:\Program Files\7-Zip\7z.exe"
    ) else if exist "C:\Program Files (x86)\7-Zip\7z.exe" (
        set "SEVENZIP=C:\Program Files (x86)\7-Zip\7z.exe"
    ) else (
        echo ERROR: 7z not found.
        echo Install: winget install 7zip.7zip
        exit /b 1
    )
) else (
    set "SEVENZIP=7z"
)
echo [OK] 7z found

REM Check Python
if defined PYTHON_EXE (
    if exist "%PYTHON_EXE%" (
        echo [OK] Python: %PYTHON_EXE%
    ) else (
        echo ERROR: Python not found at %PYTHON_EXE%
        exit /b 1
    )
) else (
    where python >nul 2>&1
    if %ERRORLEVEL% neq 0 (
        echo ERROR: Python not found.
        echo Install Python 3.8+ and add to PATH
        exit /b 1
    )
    set "PYTHON_EXE=python"
    echo [OK] Python found
)

REM Check git
where git >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: git not found.
    exit /b 1
)
echo [OK] git found

echo.

REM ========================================
REM Create Directories
REM ========================================

if not exist "%DEPS_DIR%" mkdir "%DEPS_DIR%"
if not exist "%DOWNLOADS_DIR%" mkdir "%DOWNLOADS_DIR%"

REM Set aria2 options
set "ARIA2_OPTS=-x 16 -s 32 -k 1M --check-certificate=false --file-allocation=none --allow-overwrite=true --auto-file-renaming=false"
if defined PROXY (
    set "ARIA2_OPTS=%ARIA2_OPTS% --all-proxy=%PROXY%"
    echo Using proxy: %PROXY%
)

echo.

REM ========================================
REM Download Functions
REM ========================================

REM Download and extract function
REM Usage: call :download_extract "URL" "FILENAME" "EXTRACT_DIR" "STRIP_COMPONENTS"
goto :skip_functions

:download_extract
set "DL_URL=%~1"
set "DL_FILE=%~2"
set "DL_DIR=%~3"
set "DL_STRIP=%~4"

echo Downloading: %DL_FILE%

aria2c %ARIA2_OPTS% -d "%DOWNLOADS_DIR%" -o "%DL_FILE%" "%DL_URL%"

if not exist "%DOWNLOADS_DIR%\%DL_FILE%" (
    echo ERROR: Failed to download %DL_FILE%
    exit /b 1
)

REM Extract
if exist "%DL_DIR%" (
    if "%FORCE_OVERWRITE%"=="1" (
        echo Removing existing: %DL_DIR%
        rmdir /s /q "%DL_DIR%" 2>nul
    )
)

echo Extracting: %DL_FILE%
if "%DL_STRIP%"=="0" (
    "%SEVENZIP%" x -y -o"%DL_DIR%" "%DOWNLOADS_DIR%\%DL_FILE%"
) else (
    REM Extract to temp then move
    set "TEMP_DIR=%DL_DIR%_temp"
    if exist "!TEMP_DIR!" rmdir /s /q "!TEMP_DIR!"
    mkdir "!TEMP_DIR!"
    "%SEVENZIP%" x -y -o"!TEMP_DIR!" "%DOWNLOADS_DIR%\%DL_FILE%"
    
    REM Move contents up by strip components
    set "SRC_DIR=!TEMP_DIR!"
    for /L %%i in (1,1,%DL_STRIP%) do (
        for /d %%d in ("!SRC_DIR!\*") do set "SRC_DIR=%%d"
    )
    
    mkdir "%DL_DIR%" 2>nul
    xcopy /e /i /y "!SRC_DIR!\*" "%DL_DIR%\"
    rmdir /s /q "!TEMP_DIR!"
)

echo [OK] %DL_FILE% done
exit /b 0

:skip_functions

REM ========================================
REM 1. SDL3
REM ========================================

if "%SKIP_SDL%"=="1" (
    echo [SKIP] SDL3
    goto :sdl_done
)

echo.
echo ========================================
echo [1/4] SDL3
echo ========================================

set "SDL_VERSION=3.4.2"
set "SDL_DIR=%DEPS_DIR%\SDL3"

if exist "%SDL_DIR%" (
    if "%FORCE_OVERWRITE%"=="1" (
        echo Removing existing SDL3...
        rmdir /s /q "%SDL_DIR%"
    ) else (
        echo SDL3 already exists, skipping.
        goto :sdl_done
    )
)

set "SDL_URL=https://github.com/libsdl-org/SDL/releases/download/release-%SDL_VERSION%/SDL3-devel-%SDL_VERSION%-VC.zip"
if "%USE_MIRROR%"=="1" (
    set "SDL_URL=https://ghp.ci/https://github.com/libsdl-org/SDL/releases/download/release-%SDL_VERSION%/SDL3-devel-%SDL_VERSION%-VC.zip"
)

call :download_extract "%SDL_URL%" "SDL3-devel-%SDL_VERSION%-VC.zip" "%SDL_DIR%" 1
if %ERRORLEVEL% neq 0 (
    echo ERROR: SDL3 download failed
    goto :sdl_done
)

REM Rename SDL3-3.4.2 to SDL3
if exist "%SDL_DIR%\SDL3-%SDL_VERSION%" (
    move "%SDL_DIR%\SDL3-%SDL_VERSION%" "%SDL_DIR%_tmp"
    rmdir "%SDL_DIR%"
    move "%SDL_DIR%_tmp" "%SDL_DIR%"
)

:sdl_done

REM ========================================
REM 2. vk-bootstrap
REM ========================================

if "%SKIP_VKBOOTSTRAP%"=="1" (
    echo [SKIP] vk-bootstrap
    goto :vkbootstrap_done
)

echo.
echo ========================================
echo [2/4] vk-bootstrap
echo ========================================

set "VKBOOTSTRAP_VERSION=1.4.343"
set "VKBOOTSTRAP_DIR=%DEPS_DIR%\vk-bootstrap"

if exist "%VKBOOTSTRAP_DIR%" (
    if "%FORCE_OVERWRITE%"=="1" (
        echo Removing existing vk-bootstrap...
        rmdir /s /q "%VKBOOTSTRAP_DIR%"
    ) else (
        echo vk-bootstrap already exists, skipping.
        goto :vkbootstrap_done
    )
)

set "VKBOOTSTRAP_URL=https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v%VKBOOTSTRAP_VERSION%.zip"
if "%USE_MIRROR%"=="1" (
    set "VKBOOTSTRAP_URL=https://ghp.ci/https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v%VKBOOTSTRAP_VERSION%.zip"
)

call :download_extract "%VKBOOTSTRAP_URL%" "vk-bootstrap-%VKBOOTSTRAP_VERSION%.zip" "%VKBOOTSTRAP_DIR%" 1
if %ERRORLEVEL% neq 0 (
    echo ERROR: vk-bootstrap download failed
    goto :vkbootstrap_done
)

REM Rename to standard name
if exist "%VKBOOTSTRAP_DIR%\vk-bootstrap-%VKBOOTSTRAP_VERSION%" (
    move "%VKBOOTSTRAP_DIR%\vk-bootstrap-%VKBOOTSTRAP_VERSION%" "%VKBOOTSTRAP_DIR%_tmp"
    rmdir "%VKBOOTSTRAP_DIR%"
    move "%VKBOOTSTRAP_DIR%_tmp" "%VKBOOTSTRAP_DIR%"
)

:vkbootstrap_done

REM ========================================
REM 3. VulkanMemoryAllocator
REM ========================================

if "%SKIP_VMA%"=="1" (
    echo [SKIP] VulkanMemoryAllocator
    goto :vma_done
)

echo.
echo ========================================
echo [3/4] VulkanMemoryAllocator
echo ========================================

set "VMA_VERSION=3.3.0"
set "VMA_DIR=%DEPS_DIR%\VulkanMemoryAllocator"

if exist "%VMA_DIR%" (
    if "%FORCE_OVERWRITE%"=="1" (
        echo Removing existing VulkanMemoryAllocator...
        rmdir /s /q "%VMA_DIR%"
    ) else (
        echo VulkanMemoryAllocator already exists, skipping.
        goto :vma_done
    )
)

set "VMA_URL=https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v%VMA_VERSION%.zip"
if "%USE_MIRROR%"=="1" (
    set "VMA_URL=https://ghp.ci/https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v%VMA_VERSION%.zip"
)

call :download_extract "%VMA_URL%" "VMA-%VMA_VERSION%.zip" "%VMA_DIR%" 1
if %ERRORLEVEL% neq 0 (
    echo ERROR: VMA download failed
    goto :vma_done
)

REM Rename to standard name
if exist "%VMA_DIR%\VulkanMemoryAllocator-%VMA_VERSION%" (
    move "%VMA_DIR%\VulkanMemoryAllocator-%VMA_VERSION%" "%VMA_DIR%_tmp"
    rmdir "%VMA_DIR%"
    move "%VMA_DIR%_tmp" "%VMA_DIR%"
)

:vma_done

REM ========================================
REM 4. Skia
REM ========================================

if "%SKIP_SKIA%"=="1" (
    echo [SKIP] Skia
    goto :skia_done
)

echo.
echo ========================================
echo [4/4] Skia
echo ========================================

set "SKIA_DIR=%DEPS_DIR%\skia"
set "DEPOT_TOOLS_DIR=%DEPS_DIR%\depot_tools"

REM Clone depot_tools
if not exist "%DEPOT_TOOLS_DIR%" (
    echo Cloning depot_tools...
    if "%USE_MIRROR%"=="1" (
        git clone --depth 1 https://ghp.ci/https://chromium.googlesource.com/chromium/tools/depot_tools.git "%DEPOT_TOOLS_DIR%"
    ) else (
        git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "%DEPOT_TOOLS_DIR%"
    )
)

REM Add depot_tools to PATH
set "PATH=%DEPOT_TOOLS_DIR%;%PATH%"

REM Clone Skia
if exist "%SKIA_DIR%" (
    if "%FORCE_OVERWRITE%"=="1" (
        echo Removing existing Skia...
        rmdir /s /q "%SKIA_DIR%"
    ) else (
        echo Skia already exists.
        goto :skia_deps
    )
)

echo Cloning Skia...
if "%USE_MIRROR%"=="1" (
    git clone --depth 1 https://ghp.ci/https://skia.googlesource.com/skia.git "%SKIA_DIR%"
) else (
    git clone --depth 1 https://skia.googlesource.com/skia.git "%SKIA_DIR%"
)

if not exist "%SKIA_DIR%" (
    echo ERROR: Failed to clone Skia
    goto :skia_done
)

REM Checkout specific version (optional)
cd /d "%SKIA_DIR%"
REM git checkout chrome/m146

:skia_deps

if "%SKIP_SKIA_DEPS%"=="1" (
    echo [SKIP] Skia dependencies sync
    goto :skia_done
)

echo.
echo Syncing Skia dependencies...
echo This may take a while...
echo.

cd /d "%SKIA_DIR%"

REM Sync dependencies with Python
"%PYTHON_EXE%" tools/git-sync-deps

if %ERRORLEVEL% neq 0 (
    echo.
    echo WARNING: Skia dependencies sync had errors.
    echo This might be due to emsdk SSL issues (not needed for native builds).
    echo.
    echo Try running with --skip-skia-deps to skip this step.
    echo.
)

:skia_done

REM ========================================
REM Summary
REM ========================================

echo.
echo ========================================
echo Dependency Sync Complete!
echo ========================================
echo.
echo Dependencies:
if exist "%DEPS_DIR%\SDL3" echo   [OK] SDL3
if exist "%DEPS_DIR%\vk-bootstrap" echo   [OK] vk-bootstrap
if exist "%DEPS_DIR%\VulkanMemoryAllocator" echo   [OK] VulkanMemoryAllocator
if exist "%DEPS_DIR%\skia" echo   [OK] Skia
if exist "%DEPS_DIR%\depot_tools" echo   [OK] depot_tools
echo.
echo Next steps:
echo   1. Run build_deps.bat to compile dependencies
echo   2. Run build_windows.bat to build the project
echo.

cd /d "%SCRIPT_DIR%"
endlocal
