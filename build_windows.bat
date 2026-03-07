@echo off
REM Build script for Windows
REM Requires Visual Studio 2022 or later with C++ support

setlocal enabledelayedexpansion

echo ========================================
echo Skia Graphite Renderer - Windows Build
echo ========================================

REM Check for cmake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found. Please install CMake and add to PATH.
    exit /b 1
)

REM Check for Vulkan SDK
if not defined VULKAN_SDK (
    echo WARNING: VULKAN_SDK environment variable not set.
    echo Please install Vulkan SDK from https://vulkan.lunarg.com/
)

REM Create build directory
if not exist build mkdir build
cd build

REM Configure
echo.
echo Configuring...
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="%VULKAN_SDK%" ^
    -DUSE_SYSTEM_SDL=OFF ^
    -DUSE_SYSTEM_VK_BOOTSTRAP=OFF

if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

REM Build
echo.
echo Building Release configuration...
cmake --build . --config Release -j

if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo Executable: build\Release\skia-renderer.exe
echo ========================================

endlocal
