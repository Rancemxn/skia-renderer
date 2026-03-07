# Quick Start Guide

## Windows Build (One-Click)

```cmd
REM Install prerequisites first
winget install Microsoft.VisualStudio.2022.Community
winget install Kitware.CMake
winget install aria2
winget install Git.Git

REM Set Vulkan SDK (download from https://vulkan.lunarg.com/)
set VULKAN_SDK=C:\VulkanSDK\1.3.290.0

REM Build everything
build_all.bat
```

## Step-by-Step Build

### 1. Download Dependencies
```cmd
REM Standard download
sync_deps.bat

REM Use Chinese mirrors for faster download
sync_deps.bat --mirror

REM With proxy
sync_deps.bat --proxy http://127.0.0.1:7890
```

### 2. Build Dependencies
```cmd
REM Default build
build_deps.bat

REM With custom Skia options
build_deps.bat --skia-clang-win "C:/Program Files/LLVM" --skia-enable-tools true

REM Skip certain dependencies
build_deps.bat --skip-skia --skip-sdl
```

### 3. Build Main Project
```cmd
REM Default build
build_windows.bat

REM Debug build
build_windows.bat --build-type Debug

REM Clean rebuild
build_windows.bat --clean

REM Custom dependency paths
build_windows.bat --skia-path "C:/libs/skia" --sdl3-path "C:/libs/SDL3"
```

## Skia Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `--skia-target-cpu` | x64 | Target CPU (x64, x86, arm64) |
| `--skia-clang-win` | - | Path to LLVM/Clang |
| `--skia-official-build` | true | Enable optimizations |
| `--skia-debug` | false | Debug build |
| `--skia-enable-graphite` | true | Enable Graphite backend |
| `--skia-enable-ganesh` | true | Enable Ganesh backend |
| `--skia-use-vulkan` | true | Vulkan support |
| `--skia-use-gl` | true | OpenGL support |
| `--skia-enable-tools` | false | Build Skia tools |
| `--skia-use-angle` | false | Use ANGLE |

## Example Configurations

### Minimal Build (Smallest Size)
```cmd
build_deps.bat ^
    --skia-enable-pdf false ^
    --skia-enable-tools false ^
    --skia-use-libavif false ^
    --skia-use-angle false
```

### Development Build (With Tools)
```cmd
build_deps.bat ^
    --skia-enable-tools true ^
    --build-type Debug
```

### With LLVM/Clang
```cmd
build_deps.bat --skia-clang-win "C:/Program Files/LLVM"
```

### Custom Skia Args (Manual)
```cmd
cd deps\skia
bin\gn gen out/Release --ide=vs --sln="skia" --args="target_cpu=\"x64\" clang_win=\"C:/Program Files/LLVM\" is_official_build=true is_debug=false skia_enable_ganesh=true skia_enable_graphite=true skia_use_vulkan=true skia_enable_tools=false"
ninja -C out/Release
```

## Troubleshooting

### CMake can't find Vulkan
```cmd
set VULKAN_SDK=C:\VulkanSDK\1.3.290.0
build_windows.bat --vulkan-sdk "%VULKAN_SDK%"
```

### CMake can't find SDL3
```cmd
build_windows.bat --sdl3-path "C:\path\to\SDL3"
```

### Skia build fails
- Make sure `depot_tools` is in PATH
- Try using `--skia-clang-win` with LLVM path
- Check that Python is installed

### Link errors
- Make sure all dependencies are built
- Check that Skia was built with RTTI enabled
- Verify Vulkan SDK version matches
