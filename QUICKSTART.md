# Quick Start Guide

## Prerequisites

Install these tools before building:

```powershell
# Using winget (Windows)
winget install Kitware.CMake
winget install aria2
winget install 7zip.7zip
winget install Git.Git
winget install LLVM.LLVM

# Python 3.8+
winget install Python.Python.3.12

# Vulkan SDK (download from vulkan.lunarg.com)
setx VULKAN_SDK "C:\VulkanSDK\1.3.290.0"
```

**Required:**
- LLVM/Clang + Ninja (compiler)
- CMake 3.20+
- Python 3.8+
- aria2 + 7-Zip (for downloads)
- Git
- Vulkan SDK 1.3+

## One-Click Build

```bash
python build_all.py
```

This runs all three steps automatically.

## Step-by-Step Build

### 1. Download Dependencies

```bash
# Standard download
python sync_deps.py

# With proxy
python sync_deps.py --proxy http://127.0.0.1:7890

# Keep downloaded archives
python sync_deps.py --keep-downloads
```

**Options:**
- `--skip-skia` - Skip Skia
- `--skip-sdl` - Skip SDL3
- `--skip-vkbootstrap` - Skip vk-bootstrap
- `--skip-vma` - Skip VulkanMemoryAllocator
- `--proxy URL` - Use proxy
- `--no-overwrite` - Don't overwrite existing
- `--keep-downloads` - Keep downloaded archives (deleted by default)

### 2. Build Dependencies

```bash
# Default build (uses LLVM/Clang + Ninja)
python build_deps.py

# Debug build
python build_deps.py --build-type Debug

# Clean rebuild
python build_deps.py --clean

# With Skia tools
python build_deps.py --skia-tools
```

**Options:**
- `--build-type` - Release or Debug (default: Release)
- `--target-cpu` - x64, x86, arm64 (default: x64)
- `--skia-tools` - Build Skia tools
- `--clean` - Clean before building
- `--skip-sdl` - Skip SDL3
- `--skip-vkbootstrap` - Skip vk-bootstrap
- `--skip-skia` - Skip Skia

### 3. Build Main Project

```bash
# Default build
python build_windows.py

# Debug build
python build_windows.py --build-type Debug

# Clean rebuild
python build_windows.py --clean

# Custom paths
python build_windows.py --vulkan-sdk "C:\VulkanSDK\1.3.290.0"
python build_windows.py --skia-path "C:\libs\skia" --sdl3-path "C:\libs\SDL3"
```

## Troubleshooting

### SSL Error in emsdk Download

This error is common when Skia's git-sync-deps tries to download emsdk:
```
Error: Downloading URL '...wasm-binaries.zip': SSL: UNEXPECTED_EOF_WHILE_READING
```

**Solution:** emsdk is only needed for WebAssembly builds. Native builds don't need it. The sync should still complete successfully.

### CMake Can't Find Vulkan

```bash
# Set VULKAN_SDK environment variable
set VULKAN_SDK=C:\VulkanSDK\1.3.290.0

# Or pass directly
python build_windows.py --vulkan-sdk "C:\VulkanSDK\1.3.290.0"
```

### LLVM/Clang Not Found

```powershell
winget install LLVM.LLVM
```

Ensure LLVM's bin directory is in PATH.

### Ninja Not Found

Ninja is included with LLVM. If not found:
```powershell
winget install LLVM.LLVM
```

## File Structure After Build

```
skia-renderer/
├── build/                    # Main project build
│   ├── skia-renderer.exe
│   └── compile_commands.json
├── build_deps/               # Dependency builds
├── deps/
│   ├── installed/            # Built dependencies
│   │   ├── include/
│   │   └── lib/
│   ├── SDL3/
│   ├── skia/
│   │   └── out/Release/
│   │       └── skia.lib
│   ├── vk-bootstrap/
│   ├── VulkanMemoryAllocator/
│   └── depot_tools/
└── *.py                      # Build scripts
```

## Clean Build

```bash
# Full clean rebuild
python build_all.py --clean

# Or manually
rm -rf build build_deps deps/installed
python build_deps.py --clean
python build_windows.py --clean
```
