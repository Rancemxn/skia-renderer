@echo off
setlocal enabledelayedexpansion

REM ========================================
REM Skia Renderer - Build Dependencies
REM Supports LLVM/Clang + Ninja or Visual Studio
REM ========================================

set "SCRIPT_DIR=%~dp0"
set "DEPS_DIR=%SCRIPT_DIR%deps"
set "BUILD_DIR=%SCRIPT_DIR%build_deps"
set "INSTALL_DIR=%DEPS_DIR%\installed"

REM Default build configuration
set "BUILD_TYPE=Release"
set "TARGET_CPU=x64"
set "PARALLEL_JOBS=0"
set "CLEAN_BUILD=0"
set "PYTHON_EXE="

REM Compiler configuration
set "USE_LLVM=0"
set "LLVM_PATH="
set "NINJA_PATH="
set "VS_GENERATOR="

REM ========================================
REM Skia Build Options (defaults)
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
set "SKIA_USE_LIBAVIF=false"
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

REM SDL3 options
set "SDL_VULKAN=ON"
set "SDL_OPENGL=ON"

REM ========================================
REM Parse Arguments
REM ========================================

:parse_args
if "%~1"=="" goto :end_parse

REM Build config
if /i "%~1"=="--build-type" ( set "BUILD_TYPE=%~2" & shift )
if /i "%~1"=="--target-cpu" ( set "TARGET_CPU=%~2" & shift )
if /i "%~1"=="--jobs" ( set "PARALLEL_JOBS=%~2" & shift )
if /i "%~1"=="--clean" set "CLEAN_BUILD=1"
if /i "%~1"=="--python" ( set "PYTHON_EXE=%~2" & shift )

REM Compiler options
if /i "%~1"=="--llvm" (
    set "USE_LLVM=1"
    if not "%~2"=="" (
        if not "%~2:~0,2%"=="--" (
            set "LLVM_PATH=%~2"
            shift
        )
    )
)
if /i "%~1"=="--ninja" ( set "NINJA_PATH=%~2" & shift )
if /i "%~1"=="--vs" (
    set "USE_LLVM=0"
    if not "%~2"=="" (
        set "VS_GENERATOR=%~2"
        shift
    )
)

REM Skia options
if /i "%~1"=="--skia-target-cpu" ( set "SKIA_TARGET_CPU=%~2" & shift )
if /i "%~1"=="--skia-clang-win" ( set "SKIA_CLANG_WIN=%~2" & shift )
if /i "%~1"=="--skia-official-build" ( set "SKIA_OFFICIAL_BUILD=%~2" & shift )
if /i "%~1"=="--skia-debug" ( set "SKIA_DEBUG=%~2" & shift )
if /i "%~1"=="--skia-enable-graphite" ( set "SKIA_ENABLE_GRAPHITE=%~2" & shift )
if /i "%~1"=="--skia-enable-ganesh" ( set "SKIA_ENABLE_GANESH=%~2" & shift )
if /i "%~1"=="--skia-use-vulkan" ( set "SKIA_USE_VULKAN=%~2" & shift )
if /i "%~1"=="--skia-use-gl" ( set "SKIA_USE_GL=%~2" & shift )
if /i "%~1"=="--skia-enable-tools" ( set "SKIA_ENABLE_TOOLS=%~2" & shift )
if /i "%~1"=="--skia-use-angle" ( set "SKIA_USE_ANGLE=%~2" & shift )
if /i "%~1"=="--skia-enable-pdf" ( set "SKIA_ENABLE_PDF=%~2" & shift )
if /i "%~1"=="--skia-use-libavif" ( set "SKIA_USE_LIBAVIF=%~2" & shift )
if /i "%~1"=="--skia-use-freetype" ( set "SKIA_USE_FREETYPE=%~2" & shift )

REM SDL options
if /i "%~1"=="--sdl-vulkan" ( set "SDL_VULKAN=%~2" & shift )
if /i "%~1"=="--sdl-opengl" ( set "SDL_OPENGL=%~2" & shift )

REM Skip options
if /i "%~1"=="--skip-skia" set "SKIP_SKIA=1"
if /i "%~1"=="--skip-sdl" set "SKIP_SDL=1"
if /i "%~1"=="--skip-vkbootstrap" set "SKIP_VKBOOTSTRAP=1"

if /i "%~1"=="--help" goto :show_help

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
echo Compiler Options:
echo   --llvm [PATH]        Use LLVM/Clang + Ninja (default)
echo                        Optional PATH to LLVM install dir
echo   --vs [GENERATOR]     Use Visual Studio instead
echo   --ninja PATH         Path to ninja.exe
echo.
echo Build Options:
echo   --build-type TYPE    Release or Debug (default: Release)
echo   --target-cpu CPU     x64, x86, arm64 (default: x64)
echo   --jobs N             Parallel build jobs
echo   --clean              Clean before building
echo   --python PATH        Python executable
echo.
echo Skia Options:
echo   --skia-clang-win PATH    LLVM path (default: auto-detect)
echo   --skia-official-build    Optimize (default: true)
echo   --skia-enable-graphite   Enable Graphite (default: true)
echo   --skia-use-vulkan        Vulkan support (default: true)
echo   --skia-enable-tools      Build tools (default: false)
echo.
echo Skip Options:
echo   --skip-skia          Skip Skia build
echo   --skip-sdl           Skip SDL3 build
echo   --skip-vkbootstrap   Skip vk-bootstrap build
echo.
echo Examples:
echo   %~nx0 --llvm
echo   %~nx0 --llvm "C:/Program Files/LLVM"
echo   %~nx0 --vs "Visual Studio 17 2022"
echo   %~nx0 --llvm --skia-enable-tools true
exit /b 0

:start_build

echo ========================================
echo Skia Renderer - Building Dependencies
echo ========================================
echo.

REM ========================================
REM Detect Tools
REM ========================================

echo [Detecting Build Tools]
echo.

REM Find Python
if not defined PYTHON_EXE (
    where python >nul 2>&1
    if !ERRORLEVEL! equ 0 set "PYTHON_EXE=python"
)
if defined PYTHON_EXE (
    echo [OK] Python: %PYTHON_EXE%
) else (
    echo ERROR: Python not found
    exit /b 1
)

REM Find CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found
    exit /b 1
)
echo [OK] CMake

REM Find LLVM
if not defined LLVM_PATH (
    where clang >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        for /f "tokens=*" %%i in ('where clang') do set "CLANG_EXE=%%i"
        for %%i in ("!CLANG_EXE!") do set "LLVM_PATH=%%~dpi"
        set "LLVM_PATH=!LLVM_PATH:~0,-1!"
    ) else (
        if exist "C:\Program Files\LLVM\bin\clang.exe" (
            set "LLVM_PATH=C:\Program Files\LLVM"
        )
    )
)

if defined LLVM_PATH (
    echo [OK] LLVM: %LLVM_PATH%
) else (
    echo WARNING: LLVM not found. Install LLVM for Clang support.
)

REM Find or use Ninja
if not defined NINJA_PATH (
    where ninja >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        for /f "tokens=*" %%i in ('where ninja') do set "NINJA_PATH=%%i"
    ) else (
        REM Check in depot_tools
        if exist "%DEPS_DIR%\depot_tools\ninja.exe" (
            set "NINJA_PATH=%DEPS_DIR%\depot_tools\ninja.exe"
        ) else if defined LLVM_PATH (
            if exist "!LLVM_PATH!\bin\ninja.exe" (
                set "NINJA_PATH=!LLVM_PATH!\bin\ninja.exe"
            )
        )
    )
)

if defined NINJA_PATH (
    echo [OK] Ninja: %NINJA_PATH%
    set "CMAKE_GENERATOR=Ninja"
    set "USE_LLVM=1"
) else (
    echo WARNING: Ninja not found. Will try Visual Studio.
    set "USE_LLVM=0"
)

REM Check Visual Studio if not using LLVM
if "%USE_LLVM%"=="0" (
    if not defined VS_GENERATOR (
        REM Try to detect VS
        where cl >nul 2>&1
        if !ERRORLEVEL! equ 0 (
            set "VS_GENERATOR=NMake Makefiles"
        ) else (
            REM Check for VS installations
            if exist "C:\Program Files\Microsoft Visual Studio\2022\Community" (
                set "VS_GENERATOR=Visual Studio 17 2022"
            ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional" (
                set "VS_GENERATOR=Visual Studio 17 2022"
            ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community" (
                set "VS_GENERATOR=Visual Studio 16 2019"
            )
        )
    )
    
    if defined VS_GENERATOR (
        echo [OK] Generator: %VS_GENERATOR%
        set "CMAKE_GENERATOR=%VS_GENERATOR%"
    ) else (
        echo ERROR: No suitable compiler found.
        echo Install LLVM or Visual Studio.
        exit /b 1
    )
)

echo.

REM Setup LLVM environment for Skia
if defined LLVM_PATH (
    set "SKIA_CLANG_WIN=%LLVM_PATH%"
)

REM Create directories
if "%CLEAN_BUILD%"=="1" (
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    if exist "%INSTALL_DIR%" rmdir /s /q "%INSTALL_DIR%"
)
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

echo Configuration:
echo   Build Type: %BUILD_TYPE%
echo   Target CPU: %TARGET_CPU%
echo   Generator: %CMAKE_GENERATOR%
if defined LLVM_PATH echo   LLVM: %LLVM_PATH%
echo.

REM ========================================
REM Build SDL3
REM ========================================

if "%SKIP_SDL%"=="1" (
    echo [SKIP] SDL3
    goto :sdl_done
)

echo ========================================
echo [1/3] Building SDL3
echo ========================================

if not exist "%DEPS_DIR%\SDL3" (
    echo ERROR: SDL3 not found. Run sync_deps.bat first.
    goto :sdl_done
)

REM Check if it's source or prebuilt
if not exist "%DEPS_DIR%\SDL3\CMakeLists.txt" (
    echo SDL3 appears to be prebuilt. Skipping build.
    goto :sdl_done
)

set "SDL_BUILD_DIR=%BUILD_DIR%\SDL3"
if exist "%SDL_BUILD_DIR%" if "%CLEAN_BUILD%"=="1" rmdir /s /q "%SDL_BUILD_DIR%"
if not exist "%SDL_BUILD_DIR%" mkdir "%SDL_BUILD_DIR%"

REM Configure CMake command
set "CMAKE_CMD=cmake -S "%DEPS_DIR%\SDL3" -B "%SDL_BUILD_DIR%""
set "CMAKE_CMD=%CMAKE_CMD% -G "%CMAKE_GENERATOR%""
if "%CMAKE_GENERATOR%"=="Ninja" (
    set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
)
if defined LLVM_PATH (
    set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_C_COMPILER="%LLVM_PATH%\bin\clang.exe""
    set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_CXX_COMPILER="%LLVM_PATH%\bin\clang++.exe""
)
set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%""
set "CMAKE_CMD=%CMAKE_CMD% -DSDL_VULKAN=%SDL_VULKAN%"
set "CMAKE_CMD=%CMAKE_CMD% -DSDL_OPENGL=%SDL_OPENGL%"
set "CMAKE_CMD=%CMAKE_CMD% -DSDL_TEST=OFF -DSDL_TESTS=OFF"

echo Running: %CMAKE_CMD%
%CMAKE_CMD%

if %ERRORLEVEL% neq 0 (
    echo ERROR: SDL3 CMake failed
    goto :sdl_done
)

cmake --build "%SDL_BUILD_DIR%" --config %BUILD_TYPE% --parallel %PARALLEL_JOBS%
cmake --install "%SDL_BUILD_DIR%" --config %BUILD_TYPE%

echo [OK] SDL3

:sdl_done
echo.

REM ========================================
REM Build vk-bootstrap
REM ========================================

if "%SKIP_VKBOOTSTRAP%"=="1" (
    echo [SKIP] vk-bootstrap
    goto :vkbootstrap_done
)

echo ========================================
echo [2/3] Building vk-bootstrap
echo ========================================

if not exist "%DEPS_DIR%\vk-bootstrap" (
    echo ERROR: vk-bootstrap not found. Run sync_deps.bat first.
    goto :vkbootstrap_done
)

set "VKB_BUILD_DIR=%BUILD_DIR%\vk-bootstrap"
if exist "%VKB_BUILD_DIR%" if "%CLEAN_BUILD%"=="1" rmdir /s /q "%VKB_BUILD_DIR%"
if not exist "%VKB_BUILD_DIR%" mkdir "%VKB_BUILD_DIR%"

set "CMAKE_CMD=cmake -S "%DEPS_DIR%\vk-bootstrap" -B "%VKB_BUILD_DIR%""
set "CMAKE_CMD=%CMAKE_CMD% -G "%CMAKE_GENERATOR%""
if "%CMAKE_GENERATOR%"=="Ninja" (
    set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
)
if defined LLVM_PATH (
    set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_C_COMPILER="%LLVM_PATH%\bin\clang.exe""
    set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_CXX_COMPILER="%LLVM_PATH%\bin\clang++.exe""
)
set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%""

echo Running: %CMAKE_CMD%
%CMAKE_CMD%

if %ERRORLEVEL% neq 0 (
    echo ERROR: vk-bootstrap CMake failed
    goto :vkbootstrap_done
)

cmake --build "%VKB_BUILD_DIR%" --config %BUILD_TYPE% --parallel %PARALLEL_JOBS%
cmake --install "%VKB_BUILD_DIR%" --config %BUILD_TYPE%

echo [OK] vk-bootstrap

:vkbootstrap_done
echo.

REM ========================================
REM Build Skia
REM ========================================

if "%SKIP_SKIA%"=="1" (
    echo [SKIP] Skia
    goto :skia_done
)

echo ========================================
echo [3/3] Building Skia
echo ========================================

if not exist "%DEPS_DIR%\skia" (
    echo ERROR: Skia not found. Run sync_deps.bat first.
    goto :skia_done
)

cd /d "%DEPS_DIR%\skia"

REM Setup depot_tools
if defined DEPOT_TOOLS (
    set "PATH=%DEPOT_TOOLS%;%PATH%"
) else if exist "%DEPS_DIR%\depot_tools" (
    set "PATH=%DEPS_DIR%\depot_tools;%PATH%"
)

REM Check for gn
where gn >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: gn not found. depot_tools must be in PATH.
    goto :skia_done
)

REM Build GN args
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

REM For Clang on Windows, use Clang flags
if defined SKIA_CLANG_WIN (
    set "SKIA_ARGS=%SKIA_ARGS% cc=\"clang\" cxx=\"clang++\""
    set "SKIA_ARGS=%SKIA_ARGS% extra_cflags_cc=[\"-frtti\", \"-fexceptions\"]"
) else (
    set "SKIA_ARGS=%SKIA_ARGS% extra_cflags_cc=[\"/GR\", \"/EHsc\"]"
)

echo.
echo Skia GN args:
echo %SKIA_ARGS%
echo.

REM Clean output directory
if "%CLEAN_BUILD%"=="1" (
    if exist "out\%BUILD_TYPE%" rmdir /s /q "out\%BUILD_TYPE%"
)

REM Generate build files
gn gen out/%BUILD_TYPE% --args="%SKIA_ARGS%"

if %ERRORLEVEL% neq 0 (
    echo ERROR: Skia GN generation failed
    cd /d "%SCRIPT_DIR%"
    goto :skia_done
)

REM Build
ninja -C out/%BUILD_TYPE%

if %ERRORLEVEL% neq 0 (
    echo ERROR: Skia build failed
    cd /d "%SCRIPT_DIR%"
    goto :skia_done
)

echo [OK] Skia

:skia_done

echo.
echo ========================================
echo Dependency Build Complete!
echo ========================================
echo.
echo Built libraries:

if exist "%INSTALL_DIR%\lib\SDL3.lib" echo   [OK] SDL3.lib
if exist "%INSTALL_DIR%\lib\SDL3d.lib" echo   [OK] SDL3d.lib
if exist "%INSTALL_DIR%\lib\vk-bootstrap.lib" echo   [OK] vk-bootstrap.lib
if exist "%DEPS_DIR%\skia\out\%BUILD_TYPE%\skia.lib" echo   [OK] skia.lib

echo.
echo Next: Run build_windows.bat --llvm to build the main project
echo.

cd /d "%SCRIPT_DIR%"
endlocal
