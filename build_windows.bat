@echo off
setlocal enabledelayedexpansion

REM ========================================
REM Skia Graphite Renderer - Windows Build
REM Supports LLVM/Clang + Ninja or Visual Studio
REM ========================================

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build"
set "DEPS_DIR=%SCRIPT_DIR%deps"
set "INSTALL_DIR=%DEPS_DIR%\installed"

REM Default configuration
set "BUILD_TYPE=Release"
set "TARGET_CPU=x64"
set "PARALLEL_JOBS=0"
set "CLEAN_BUILD=0"

REM Compiler configuration
set "USE_LLVM=0"
set "LLVM_PATH="
set "NINJA_PATH="
set "VS_GENERATOR="

REM Custom paths (optional)
set "VULKAN_SDK_PATH="
set "SDL3_PATH="
set "SKIA_PATH="
set "VKBOOTSTRAP_PATH="
set "VMA_PATH="
set "EXTRA_CMAKE_ARGS="

REM ========================================
REM Parse Arguments
REM ========================================

:parse_args
if "%~1"=="" goto :end_parse

if /i "%~1"=="--build-type" ( set "BUILD_TYPE=%~2" & shift )
if /i "%~1"=="--target-cpu" ( set "TARGET_CPU=%~2" & shift )
if /i "%~1"=="--jobs" ( set "PARALLEL_JOBS=%~2" & shift )
if /i "%~1"=="--clean" set "CLEAN_BUILD=1"

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

REM Custom paths
if /i "%~1"=="--vulkan-sdk" ( set "VULKAN_SDK_PATH=%~2" & shift )
if /i "%~1"=="--sdl3-path" ( set "SDL3_PATH=%~2" & shift )
if /i "%~1"=="--skia-path" ( set "SKIA_PATH=%~2" & shift )
if /i "%~1"=="--vkbootstrap-path" ( set "VKBOOTSTRAP_PATH=%~2" & shift )
if /i "%~1"=="--vma-path" ( set "VMA_PATH=%~2" & shift )
if /i "%~1"=="--cmake-args" ( set "EXTRA_CMAKE_ARGS=%~2" & shift )

if /i "%~1"=="--help" goto :show_help

shift
goto :parse_args
:end_parse

goto :start_build

:show_help
echo ========================================
echo Skia Graphite Renderer - Build Script
echo ========================================
echo.
echo Usage: %~nx0 [options]
echo.
echo Compiler Options:
echo   --llvm [PATH]        Use LLVM/Clang + Ninja (default)
echo   --vs [GENERATOR]     Use Visual Studio
echo   --ninja PATH         Path to ninja.exe
echo.
echo Build Options:
echo   --build-type TYPE    Release or Debug (default: Release)
echo   --target-cpu CPU     x64, x86, arm64 (default: x64)
echo   --jobs N             Parallel jobs
echo   --clean              Clean before building
echo.
echo Custom Paths:
echo   --vulkan-sdk PATH    Vulkan SDK path
echo   --sdl3-path PATH     SDL3 path
echo   --skia-path PATH     Skia path
echo   --vkbootstrap-path PATH vk-bootstrap path
echo   --vma-path PATH      VMA path
echo   --cmake-args ARGS    Extra CMake arguments
echo.
echo Examples:
echo   %~nx0 --llvm
echo   %~nx0 --llvm "C:/Program Files/LLVM" --build-type Debug
echo   %~nx0 --vs "Visual Studio 17 2022"
exit /b 0

:start_build

echo ========================================
echo Skia Graphite Renderer - Windows Build
echo ========================================
echo.

REM ========================================
REM Detect Tools
REM ========================================

echo [Detecting Build Tools]
echo.

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
    echo WARNING: LLVM not found
)

REM Find Ninja
if not defined NINJA_PATH (
    where ninja >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        for /f "tokens=*" %%i in ('where ninja') do set "NINJA_PATH=%%i"
    ) else (
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
    echo WARNING: Ninja not found
    set "USE_LLVM=0"
)

REM Check Visual Studio
if "%USE_LLVM%"=="0" (
    if not defined VS_GENERATOR (
        if exist "C:\Program Files\Microsoft Visual Studio\2022\Community" (
            set "VS_GENERATOR=Visual Studio 17 2022"
        ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional" (
            set "VS_GENERATOR=Visual Studio 17 2022"
        )
    )
    
    if defined VS_GENERATOR (
        echo [OK] Generator: %VS_GENERATOR%
        set "CMAKE_GENERATOR=%VS_GENERATOR%"
    ) else (
        echo ERROR: No suitable compiler found
        echo Install LLVM with Ninja or Visual Studio
        exit /b 1
    )
)

echo.

REM ========================================
REM Determine Dependency Paths
REM ========================================

if defined VULKAN_SDK_PATH (
    set "VK_SDK=%VULKAN_SDK_PATH%"
) else if defined VULKAN_SDK (
    set "VK_SDK=%VULKAN_SDK%"
) else (
    echo WARNING: VULKAN_SDK not set
    set "VK_SDK="
)

if defined SDL3_PATH (
    set "SDL3_DIR=%SDL3_PATH%"
) else if exist "%INSTALL_DIR%\lib\cmake\SDL3" (
    set "SDL3_DIR=%INSTALL_DIR%\lib\cmake\SDL3"
) else if exist "%DEPS_DIR%\SDL3\lib\cmake\SDL3" (
    set "SDL3_DIR=%DEPS_DIR%\SDL3\lib\cmake\SDL3"
) else (
    set "SDL3_DIR=%DEPS_DIR%\SDL3"
)

if defined SKIA_PATH (
    set "SKIA_DIR=%SKIA_PATH%"
) else (
    set "SKIA_DIR=%DEPS_DIR%\skia"
)

if defined VKBOOTSTRAP_PATH (
    set "VKBOOTSTRAP_DIR=%VKBOOTSTRAP_PATH%"
) else if exist "%INSTALL_DIR%\include\vk-bootstrap" (
    set "VKBOOTSTRAP_DIR=%INSTALL_DIR%"
) else (
    set "VKBOOTSTRAP_DIR=%DEPS_DIR%\vk-bootstrap"
)

if defined VMA_PATH (
    set "VMA_DIR=%VMA_PATH%"
) else (
    set "VMA_DIR=%DEPS_DIR%\VulkanMemoryAllocator"
)

echo Configuration:
echo   Build Type: %BUILD_TYPE%
echo   Target CPU: %TARGET_CPU%
echo   Generator: %CMAKE_GENERATOR%
if defined LLVM_PATH echo   LLVM: %LLVM_PATH%
echo.
echo Dependencies:
echo   Vulkan SDK: %VK_SDK%"
echo   SDL3: %SDL3_DIR%"
echo   Skia: %SKIA_DIR%"
echo   vk-bootstrap: %VKBOOTSTRAP_DIR%"
echo.

REM ========================================
REM Clean Build
REM ========================================

if "%CLEAN_BUILD%"=="1" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM ========================================
REM Configure CMake
REM ========================================

echo.
echo Configuring project...
echo.

set "CMAKE_ARGS=-G "%CMAKE_GENERATOR%""

if "%CMAKE_GENERATOR%"=="Ninja" (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
)

if defined LLVM_PATH (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_C_COMPILER="%LLVM_PATH%\bin\clang.exe""
    set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_CXX_COMPILER="%LLVM_PATH%\bin\clang++.exe""
)

set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

if defined VK_SDK (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DVULKAN_SDK=%VK_SDK%"
    set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_PREFIX_PATH=%VK_SDK%"
)

set "CMAKE_ARGS=%CMAKE_ARGS% -DSDL3_DIR=%SDL3_DIR%"
set "CMAKE_ARGS=%CMAKE_ARGS% -DSKIA_DIR=%SKIA_DIR%"
set "CMAKE_ARGS=%CMAKE_ARGS% -DVKBOOTSTRAP_DIR=%VKBOOTSTRAP_DIR%"
set "CMAKE_ARGS=%CMAKE_ARGS% -DVMA_DIR=%VMA_DIR%"

if defined EXTRA_CMAKE_ARGS (
    set "CMAKE_ARGS=%CMAKE_ARGS% %EXTRA_CMAKE_ARGS%"
)

echo Running: cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" %CMAKE_ARGS%
echo.

cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" %CMAKE_ARGS%

if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: CMake configuration failed
    echo.
    echo Troubleshooting:
    echo   1. Set VULKAN_SDK environment variable
    echo   2. Run sync_deps.bat and build_deps.bat first
    echo   3. Use --vulkan-sdk, --sdl3-path options
    exit /b 1
)

REM ========================================
REM Build
REM ========================================

echo.
echo Building %BUILD_TYPE%...
echo.

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel %PARALLEL_JOBS%

if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Build failed
    exit /b 1
)

REM ========================================
REM Success
REM ========================================

echo.
echo ========================================
echo Build Complete!
echo ========================================
echo.

if "%CMAKE_GENERATOR%"=="Ninja" (
    set "EXE_PATH=%BUILD_DIR%\skia-renderer.exe"
) else (
    set "EXE_PATH=%BUILD_DIR%\%BUILD_TYPE%\skia-renderer.exe"
)

if exist "%EXE_PATH%" (
    echo Executable: %EXE_PATH%
    echo.
    echo Run: "%EXE_PATH%"
) else (
    echo Executable should be in: %BUILD_DIR%
)

echo.

endlocal
