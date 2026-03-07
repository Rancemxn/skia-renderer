@echo off
setlocal enabledelayedexpansion

REM ========================================
REM Skia Renderer - Build Dependencies
REM ========================================

set "DEPS_DIR=%~dp0deps"
set "BUILD_DIR=%~dp0build_deps"
set "INSTALL_DIR=%~dp0deps\installed"

REM Default build configuration
set "BUILD_TYPE=Release"
set "TARGET_CPU=x64"
set "VS_GENERATOR=Visual Studio 17 2022"
set "PARALLEL_JOBS=0"

REM ========================================
REM Skia Build Options (can be overridden)
REM ========================================
set "SKIA_TARGET_CPU=x64"
set "SKIA_CLANG_WIN="
set "SKIA_OFFICIAL_BUILD=true"
set "SKIA_DEBUG=false"
set "SKIA_USE_SYSTEM_EXPAT=false"
set "SKIA_USE_SYSTEM_HARFBUZZ=false"
set "SKIA_USE_SYSTEM_ICU=false"
set "SKIA_USE_SYSTEM_LIBJPEG_TURBO=false"
set "SKIA_USE_SYSTEM_LIBPNG=false"
set "SKIA_USE_SYSTEM_FREETYPE2=false"
set "SKIA_USE_SYSTEM_LIBWEBP=false"
set "SKIA_USE_SYSTEM_ZLIB=false"
set "SKIA_ENABLE_GANESH=true"
set "SKIA_ENABLE_GRAPHITE=true"
set "SKIA_ENABLE_PDF=true"
set "SKIA_USE_GL=true"
set "SKIA_USE_VULKAN=true"
set "SKIA_USE_LIBJPEG_TURBO_DECODE=true"
set "SKIA_USE_LIBJPEG_TURBO_ENCODE=true"
set "SKIA_USE_LIBPNG_DECODE=true"
set "SKIA_USE_LIBPNG_ENCODE=true"
set "SKIA_USE_LIBWEBP_DECODE=true"
set "SKIA_USE_LIBWEBP_ENCODE=true"
set "SKIA_USE_LIBAVIF=true"
set "SKIA_USE_WUFFS=true"
set "SKIA_USE_FREETYPE=true"
set "SKIA_ENABLE_TOOLS=false"
set "SKIA_COMPILE_MODULES=true"
set "SKIA_ENABLE_PRECOMPILE=true"
set "SKIA_USE_ZLIB=true"
set "SKIA_USE_ICU=true"
set "SKIA_USE_EXPAT=true"
set "SKIA_USE_ANGLE=false"
set "SKIA_ENABLE_VELLO_SHADERS=false"
set "SKIA_EXTRA_CFLAGS="
set "SKIA_EXTRA_CXXFLAGS="

REM ========================================
REM SDL3 Build Options
REM ========================================
set "SDL_VULKAN=ON"
set "SDL_OPENGL=ON"
set "SDL_RENDER=ON"
set "SDL_ATOMIC=ON"
set "SDL_THREADS=ON"
set "SDL_EVENTS=ON"
set "SDL_JOYSTICK=ON"
set "SDL_HIDAPI=ON"
set "SDL_POWER=ON"
set "SDL_FILESYSTEM=ON"
set "SDL_DIALOG=OFF"
set "SDL_EXTRA_CMAKE_ARGS="

REM ========================================
REM Parse Arguments
REM ========================================

:parse_args
if "%~1"=="" goto :end_parse

REM Skia options
if /i "%~1"=="--skia-target-cpu" (
    set "SKIA_TARGET_CPU=%~2"
    shift
)
if /i "%~1"=="--skia-clang-win" (
    set "SKIA_CLANG_WIN=%~2"
    shift
)
if /i "%~1"=="--skia-official-build" (
    set "SKIA_OFFICIAL_BUILD=%~2"
    shift
)
if /i "%~1"=="--skia-debug" (
    set "SKIA_DEBUG=%~2"
    shift
)
if /i "%~1"=="--skia-enable-graphite" (
    set "SKIA_ENABLE_GRAPHITE=%~2"
    shift
)
if /i "%~1"=="--skia-enable-ganesh" (
    set "SKIA_ENABLE_GANESH=%~2"
    shift
)
if /i "%~1"=="--skia-use-vulkan" (
    set "SKIA_USE_VULKAN=%~2"
    shift
)
if /i "%~1"=="--skia-use-gl" (
    set "SKIA_USE_GL=%~2"
    shift
)
if /i "%~1"=="--skia-enable-tools" (
    set "SKIA_ENABLE_TOOLS=%~2"
    shift
)
if /i "%~1"=="--skia-use-angle" (
    set "SKIA_USE_ANGLE=%~2"
    shift
)
if /i "%~1"=="--skia-extra-cflags" (
    set "SKIA_EXTRA_CFLAGS=%~2"
    shift
)
if /i "%~1"=="--skia-extra-cxxflags" (
    set "SKIA_EXTRA_CXXFLAGS=%~2"
    shift
)

REM SDL3 options
if /i "%~1"=="--sdl-vulkan" (
    set "SDL_VULKAN=%~2"
    shift
)
if /i "%~1"=="--sdl-opengl" (
    set "SDL_OPENGL=%~2"
    shift
)
if /i "%~1"=="--sdl-extra-cmake" (
    set "SDL_EXTRA_CMAKE_ARGS=%~2"
    shift
)

REM General options
if /i "%~1"=="--build-type" (
    set "BUILD_TYPE=%~2"
    shift
)
if /i "%~1"=="--target-cpu" (
    set "TARGET_CPU=%~2"
    shift
)
if /i "%~1"=="--generator" (
    set "VS_GENERATOR=%~2"
    shift
)
if /i "%~1"=="--jobs" (
    set "PARALLEL_JOBS=%~2"
    shift
)

REM Skip options
if /i "%~1"=="--skip-skia" set "SKIP_SKIA=1"
if /i "%~1"=="--skip-sdl" set "SKIP_SDL=1"
if /i "%~1"=="--skip-vkbootstrap" set "SKIP_VKBOOTSTRAP=1"

if /i "%~1"=="--help" (
    goto :show_help
)

shift
goto :parse_args
:end_parse

goto :start_build

:show_help
echo ========================================
echo Build Dependencies for Skia Renderer
echo ========================================
echo.
echo Usage: %~nx0 [options]
echo.
echo General Options:
echo   --build-type TYPE      Release or Debug (default: Release)
echo   --target-cpu CPU       x64 or x86 (default: x64)
echo   --generator GEN        CMake generator (default: "Visual Studio 17 2022")
echo   --jobs N               Parallel jobs for build (default: auto)
echo.
echo Skia Options:
echo   --skia-target-cpu CPU      Target CPU: x64, x86, arm64 (default: x64)
echo   --skia-clang-win PATH      Path to LLVM/Clang (e.g., "C:/Program Files/LLVM")
echo   --skia-official-build BOOL Optimize build (default: true)
echo   --skia-debug BOOL          Debug build (default: false)
echo   --skia-enable-graphite BOOL Enable Graphite backend (default: true)
echo   --skia-enable-ganesh BOOL  Enable Ganesh backend (default: true)
echo   --skia-use-vulkan BOOL     Enable Vulkan support (default: true)
echo   --skia-use-gl BOOL         Enable OpenGL support (default: true)
echo   --skia-enable-tools BOOL   Build Skia tools (default: false)
echo   --skia-use-angle BOOL      Use ANGLE (default: false)
echo   --skia-extra-cflags FLAGS  Extra C compiler flags
echo   --skia-extra-cxxflags FLAGS Extra C++ compiler flags
echo.
echo SDL3 Options:
echo   --sdl-vulkan BOOL      Enable Vulkan (default: ON)
echo   --sdl-opengl BOOL      Enable OpenGL (default: ON)
echo   --sdl-extra-cmake ARGS Extra CMake arguments
echo.
echo Skip Options:
echo   --skip-skia           Skip building Skia
echo   --skip-sdl            Skip building SDL3
echo   --skip-vkbootstrap    Skip building vk-bootstrap
echo.
echo Example:
echo   %~nx0 --skia-clang-win "C:/Program Files/LLVM" --skia-enable-tools true
echo.
echo   %~nx0 --skip-skia --build-type Debug
exit /b 0

:start_build

echo ========================================
echo Skia Renderer - Building Dependencies
echo ========================================
echo.
echo Configuration:
echo   Build Type: %BUILD_TYPE%
echo   Target CPU: %TARGET_CPU%
echo   Generator: %VS_GENERATOR%
echo.

REM Check for cmake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found. Please install CMake.
    exit /b 1
)

REM Create directories
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

REM ========================================
REM Build SDL3
REM ========================================
if "%SKIP_SDL%"=="1" (
    echo [SKIP] SDL3
    goto :sdl_done
)

echo [1/3] Building SDL3...

if not exist "%DEPS_DIR%\SDL3" (
    echo ERROR: SDL3 not found. Run sync_deps.bat first.
    goto :sdl_done
)

set "SDL_BUILD_DIR=%BUILD_DIR%\SDL3"
if not exist "%SDL_BUILD_DIR%" mkdir "%SDL_BUILD_DIR%"

cmake -S "%DEPS_DIR%\SDL3" -B "%SDL_BUILD_DIR%" ^
    -G "%VS_GENERATOR%" -A %TARGET_CPU% ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DSDL_VULKAN=%SDL_VULKAN% ^
    -DSDL_OPENGL=%SDL_OPENGL% ^
    -DSDL_RENDER=%SDL_RENDER% ^
    -DSDL_TEST=OFF ^
    -DSDL_TESTS=OFF ^
    %SDL_EXTRA_CMAKE_ARGS%

if %ERRORLEVEL% neq 0 (
    echo ERROR: SDL3 CMake configuration failed.
    goto :sdl_done
)

cmake --build "%SDL_BUILD_DIR%" --config %BUILD_TYPE% --parallel %PARALLEL_JOBS%
cmake --install "%SDL_BUILD_DIR%" --config %BUILD_TYPE%

echo SDL3 build complete!
:sdl_done
echo.

REM ========================================
REM Build vk-bootstrap
REM ========================================
if "%SKIP_VKBOOTSTRAP%"=="1" (
    echo [SKIP] vk-bootstrap
    goto :vkbootstrap_done
)

echo [2/3] Building vk-bootstrap...

if not exist "%DEPS_DIR%\vk-bootstrap" (
    echo ERROR: vk-bootstrap not found. Run sync_deps.bat first.
    goto :vkbootstrap_done
)

set "VKBOOTSTRAP_BUILD_DIR=%BUILD_DIR%\vk-bootstrap"
if not exist "%VKBOOTSTRAP_BUILD_DIR%" mkdir "%VKBOOTSTRAP_BUILD_DIR%"

cmake -S "%DEPS_DIR%\vk-bootstrap" -B "%VKBOOTSTRAP_BUILD_DIR%" ^
    -G "%VS_GENERATOR%" -A %TARGET_CPU% ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE%

if %ERRORLEVEL% neq 0 (
    echo ERROR: vk-bootstrap CMake configuration failed.
    goto :vkbootstrap_done
)

cmake --build "%VKBOOTSTRAP_BUILD_DIR%" --config %BUILD_TYPE% --parallel %PARALLEL_JOBS%
cmake --install "%VKBOOTSTRAP_BUILD_DIR%" --config %BUILD_TYPE%

echo vk-bootstrap build complete!
:vkbootstrap_done
echo.

REM ========================================
REM Build Skia
REM ========================================
if "%SKIP_SKIA%"=="1" (
    echo [SKIP] Skia
    goto :skia_done
)

echo [3/3] Building Skia...

if not exist "%DEPS_DIR%\skia" (
    echo ERROR: Skia not found. Run sync_deps.bat first.
    goto :skia_done
)

cd /d "%DEPS_DIR%\skia"

REM Check for depot_tools
if not defined DEPOT_TOOLS (
    if exist "%DEPS_DIR%\depot_tools" (
        set "DEPOT_TOOLS=%DEPS_DIR%\depot_tools"
    ) else (
        echo ERROR: depot_tools not found. Set DEPOT_TOOLS environment variable.
        goto :skia_done
    )
)
set "PATH=%DEPOT_TOOLS%;%PATH%"

REM Check for gn
where gn >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: gn not found. Make sure depot_tools is in PATH.
    goto :skia_done
)

REM Build Skia args string
set "SKIA_ARGS=target_cpu=\"%SKIA_TARGET_CPU%\""

if defined SKIA_CLANG_WIN (
    set "SKIA_ARGS=%SKIA_ARGS% clang_win=\"%SKIA_CLANG_WIN%\""
)

set "SKIA_ARGS=%SKIA_ARGS% is_official_build=%SKIA_OFFICIAL_BUILD%"
set "SKIA_ARGS=%SKIA_ARGS% is_debug=%SKIA_DEBUG%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_system_expat=%SKIA_USE_SYSTEM_EXPAT%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_system_harfbuzz=%SKIA_USE_SYSTEM_HARFBUZZ%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_system_icu=%SKIA_USE_SYSTEM_ICU%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_system_libjpeg_turbo=%SKIA_USE_SYSTEM_LIBJPEG_TURBO%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_system_libpng=%SKIA_USE_SYSTEM_LIBPNG%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_system_freetype2=%SKIA_USE_SYSTEM_FREETYPE2%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_system_libwebp=%SKIA_USE_SYSTEM_LIBWEBP%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_system_zlib=%SKIA_USE_SYSTEM_ZLIB%"
set "SKIA_ARGS=%SKIA_ARGS% skia_enable_ganesh=%SKIA_ENABLE_GANESH%"
set "SKIA_ARGS=%SKIA_ARGS% skia_enable_graphite=%SKIA_ENABLE_GRAPHITE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_enable_pdf=%SKIA_ENABLE_PDF%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_gl=%SKIA_USE_GL%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_vulkan=%SKIA_USE_VULKAN%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_libjpeg_turbo_decode=%SKIA_USE_LIBJPEG_TURBO_DECODE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_libjpeg_turbo_encode=%SKIA_USE_LIBJPEG_TURBO_ENCODE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_libpng_decode=%SKIA_USE_LIBPNG_DECODE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_libpng_encode=%SKIA_USE_LIBPNG_ENCODE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_libwebp_decode=%SKIA_USE_LIBWEBP_DECODE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_libwebp_encode=%SKIA_USE_LIBWEBP_ENCODE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_libavif=%SKIA_USE_LIBAVIF%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_wuffs=%SKIA_USE_WUFFS%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_freetype=%SKIA_USE_FREETYPE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_enable_tools=%SKIA_ENABLE_TOOLS%"
set "SKIA_ARGS=%SKIA_ARGS% skia_compile_modules=%SKIA_COMPILE_MODULES%"
set "SKIA_ARGS=%SKIA_ARGS% skia_enable_precompile=%SKIA_ENABLE_PRECOMPILE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_zlib=%SKIA_USE_ZLIB%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_icu=%SKIA_USE_ICU%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_expat=%SKIA_USE_EXPAT%"
set "SKIA_ARGS=%SKIA_ARGS% skia_use_angle=%SKIA_USE_ANGLE%"
set "SKIA_ARGS=%SKIA_ARGS% skia_enable_vello_shaders=%SKIA_ENABLE_VELLO_SHADERS%"

if defined SKIA_EXTRA_CFLAGS (
    set "SKIA_ARGS=%SKIA_ARGS% extra_cflags=[\"%SKIA_EXTRA_CFLAGS%\"]"
)
if defined SKIA_EXTRA_CXXFLAGS (
    set "SKIA_ARGS=%SKIA_ARGS% extra_cflags_cc=[\"%SKIA_EXTRA_CXXFLAGS%\"]"
)

REM Also add RTTI for compatibility
set "SKIA_ARGS=%SKIA_ARGS% extra_cflags_cc=[\"/GR\", \"/EHsc\"]"

echo.
echo Skia GN args:
echo %SKIA_ARGS%
echo.

REM Generate build files
gn gen out/%BUILD_TYPE% --args="%SKIA_ARGS%"

if %ERRORLEVEL% neq 0 (
    echo ERROR: Skia GN generation failed.
    goto :skia_done
)

REM Build Skia
ninja -C out/%BUILD_TYPE%

if %ERRORLEVEL% neq 0 (
    echo ERROR: Skia build failed.
    goto :skia_done
)

echo Skia build complete!
:skia_done
echo.

REM ========================================
REM Summary
REM ========================================
echo ========================================
echo Dependency Build Complete!
echo ========================================
echo.
echo Built libraries:
if exist "%INSTALL_DIR%\lib" (
    dir /b "%INSTALL_DIR%\lib\*.lib" 2>nul
)
if exist "%DEPS_DIR%\skia\out\%BUILD_TYPE%\skia.lib" (
    echo skia.lib (in deps\skia\out\%BUILD_TYPE%)
)
echo.
echo Next step: Run build_windows.bat to build the main project.
echo.

cd /d "%~dp0"
endlocal
