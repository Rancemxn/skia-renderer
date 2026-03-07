# Skia Graphite Renderer

A high-performance 2D rendering application using **SDL3**, **Skia Graphite**, **vk-bootstrap**, **VMA**, and **Vulkan 1.3**.

## Features

- 🎨 Modern Vulkan 1.3 rendering backend
- ⚡ Skia Graphite for hardware-accelerated 2D graphics
- 🖼️ SDL3 for cross-platform window management
- 🔧 vk-bootstrap for easy Vulkan initialization
- 💾 VMA (Vulkan Memory Allocator) for efficient memory management
- 📦 C++20 with clean architecture
- 🛠️ Flexible build scripts with extensive customization options

## Quick Start

### Windows (One-Click Build)

```cmd
REM Prerequisites: VS2022, CMake, aria2, Git
build_all.bat
```

### Step-by-Step

```cmd
REM 1. Download dependencies
sync_deps.bat

REM 2. Build dependencies  
build_deps.bat

REM 3. Build main project
build_windows.bat
```

See [QUICKSTART.md](QUICKSTART.md) for detailed options.

## Requirements

| Tool | Version | Notes |
|------|---------|-------|
| Visual Studio | 2022+ | With C++ development workload |
| CMake | 3.20+ | Build system |
| Vulkan SDK | 1.3+ | Graphics API |
| Git | Latest | For cloning repos |
| aria2 | Latest | Fast parallel downloads |
| Python | 3.8+ | For Skia build |

## Build Scripts

| Script | Description |
|--------|-------------|
| `build_all.bat` | One-click complete build |
| `sync_deps.bat` | Download all dependencies with aria2 |
| `build_deps.bat` | Build SDL3, vk-bootstrap, VMA, Skia |
| `build_windows.bat` | Build main project |
| `build_unix.sh` | Build script for Linux/macOS |

## Customizing Builds

### Skia Options

```cmd
REM Enable Skia tools for debugging
build_deps.bat --skia-enable-tools true

REM Use LLVM Clang compiler
build_deps.bat --skia-clang-win "C:/Program Files/LLVM"

REM Build for ARM64
build_deps.bat --skia-target-cpu arm64

REM Full custom args
cd deps\skia
bin\gn gen out/Release --args="target_cpu=\"x64\" clang_win=\"C:/Program Files/LLVM\" is_official_build=true skia_enable_graphite=true skia_use_vulkan=true"
```

### SDL3 Options

```cmd
build_deps.bat --sdl-vulkan ON --sdl-opengl ON
```

### Main Project Options

```cmd
REM Debug build
build_windows.bat --build-type Debug

REM Clean rebuild
build_windows.bat --clean

REM Custom paths
build_windows.bat --vulkan-sdk "C:\VulkanSDK\1.3.290.0" --skia-path "C:\libs\skia"
```

## Project Structure

```
skia-renderer/
├── src/
│   ├── main.cpp                 # Entry point
│   ├── core/
│   │   ├── Application.h/cpp    # Main application class
│   └── renderer/
│       ├── VulkanContext.h/cpp  # Vulkan initialization (vk-bootstrap)
│       ├── Swapchain.h/cpp      # Swapchain management
│       └── SkiaRenderer.h/cpp   # Skia Graphite rendering
├── deps/                        # Dependencies (downloaded)
├── build_deps.bat               # Build dependencies
├── sync_deps.bat                # Download dependencies
├── build_windows.bat            # Build main project
├── build_all.bat                # One-click build
└── CMakeLists.txt               # CMake configuration
```

## Skia Build Arguments Reference

Key Skia GN arguments used:

| Argument | Default | Description |
|----------|---------|-------------|
| `target_cpu` | x64 | Target architecture |
| `is_official_build` | true | Optimized release build |
| `skia_enable_graphite` | true | Enable Graphite backend |
| `skia_enable_ganesh` | true | Enable Ganesh backend |
| `skia_use_vulkan` | true | Vulkan support |
| `skia_use_gl` | true | OpenGL support |
| `skia_enable_tools` | false | Build Skia tools |
| `skia_enable_pdf` | true | PDF support |
| `skia_use_system_*` | false | Use system libraries |

## Running

```cmd
REM From build directory
build\Release\skia-renderer.exe

REM With custom window size
build\Release\skia-renderer.exe --width 1920 --height 1080
```

Press **ESC** to exit.

## Troubleshooting

### "VULKAN_SDK not set"
```cmd
set VULKAN_SDK=C:\VulkanSDK\1.3.290.0
```

### "aria2c not found"
```cmd
winget install aria2
```

### "depot_tools not found"
```cmd
set DEPOT_TOOLS=C:\path\to\depot_tools
set PATH=%DEPOT_TOOLS%;%PATH%
```

### CMake can't find dependencies
```cmd
build_windows.bat --sdl3-path "path\to\SDL3" --skia-path "path\to\skia"
```

## Dependencies

| Library | Version | License |
|---------|---------|---------|
| SDL3 | 3.4.2+ | Zlib |
| vk-bootstrap | 1.4.343+ | MIT |
| VulkanMemoryAllocator | 3.3.0+ | MIT |
| Skia | chrome/m146 | BSD-3-Clause |

## License

MIT License
