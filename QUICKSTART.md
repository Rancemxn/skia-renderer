# Quick Start Guide

## Prerequisites

Install these tools before building:

### Windows
```powershell
# Using winget
winget install Kitware.CMake
winget install aria2
winget install 7zip.7zip
winget install Git.Git
winget install LLVM.LLVM

# Python 3.8+
winget install Python.Python.3.12

# Vulkan SDK (download from vulkan.lunarg.com)
# Set environment variable:
setx VULKAN_SDK "C:\VulkanSDK\1.3.290.0"

# sccache (optional, speeds up rebuilds)
winget install Mozilla.sccache
```

### Linux (Ubuntu/Debian)
```bash
sudo apt install cmake ninja-build clang aria2 p7zip-full git

# Python 3.8+
sudo apt install python3

# Vulkan SDK (download from vulkan.lunarg.com)
export VULKAN_SDK=/path/to/vulkan-sdk

# sccache (optional)
cargo install sccache
```

### macOS
```bash
brew install cmake ninja llvm aria2 p7zip git

# Python 3.8+
brew install python3

# Vulkan SDK (download from vulkan.lunarg.com)
export VULKAN_SDK=/path/to/vulkan-sdk

# sccache (optional)
cargo install sccache
```

## Requirements

| Tool | Required | Notes |
|------|----------|-------|
| LLVM/Clang | ✓ | Compiler |
| CMake 3.20+ | ✓ | Build system |
| Ninja | ✓ | Build tool (included with LLVM) |
| Python 3.8+ | ✓ | Build scripts |
| Vulkan SDK 1.3+ | ✓ | Graphics API |
| Git | ✓ | For cloning repos |
| aria2 | ✓ | Fast downloads |
| 7-Zip | ✓ | Archive extraction |
| sccache | Optional | Compiler cache for faster rebuilds |

## One-Click Build

```bash
python build_all.py
```

## Step-by-Step Build

### 1. Download Dependencies

```bash
python sync.py

# With proxy
python sync.py --proxy http://127.0.0.1:7890

# Keep downloaded archives
python sync.py --keep-downloads
```

**Options:**
- `--skip-skia` - Skip Skia
- `--skip-sdl` - Skip SDL3
- `--skip-vkbootstrap` - Skip vk-bootstrap
- `--skip-vma` - Skip VulkanMemoryAllocator
- `--proxy URL` - Use proxy for downloads
- `--no-overwrite` - Don't overwrite existing directories
- `--keep-downloads` - Keep downloaded archives (deleted by default)

### 2. Build Dependencies

```bash
python build_deps.py

# Debug build
python build_deps.py --build-type Debug

# Clean rebuild
python build_deps.py --clean

# Build Skia tools
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
python build.py

# Debug build
python build.py --build-type Debug

# Clean rebuild
python build.py --clean

# Custom paths
python build.py --vulkan-sdk "C:\VulkanSDK\1.3.290.0"
```

## sccache (Optional)

sccache caches compiled objects, speeding up rebuilds significantly.

### Install sccache

**Windows:**
```powershell
winget install Mozilla.sccache
```

**Linux/macOS:**
```bash
cargo install sccache
```

### Configure sccache

sccache is automatically detected and used when available.

You can configure sccache by setting environment variables:

```bash
# Local disk cache (default)
export SCCACHE_DIR=~/.cache/sccache
export SCCACHE_MAX_SIZE=10G

# S3 cache (for teams)
export SCCACHE_BUCKET=my-sccache-bucket
export AWS_ACCESS_KEY_ID=xxx
export AWS_SECRET_ACCESS_KEY=xxx
```

## Troubleshooting

### LLVM/Clang Not Found

The build scripts require LLVM/Clang as the compiler.

```powershell
# Windows
winget install LLVM.LLVM

# Linux
sudo apt install clang

# macOS
brew install llvm
```

### Ninja Not Found

Ninja is typically included with LLVM. If not found:

```bash
# Linux
sudo apt install ninja-build

# macOS
brew install ninja
```

### CMake Can't Find Vulkan

```bash
# Set VULKAN_SDK environment variable
export VULKAN_SDK=/path/to/vulkan-sdk  # Linux/macOS
set VULKAN_SDK=C:\VulkanSDK\1.3.290.0  # Windows

# Or pass directly
python build.py --vulkan-sdk "C:\VulkanSDK\1.3.290.0"
```

### SSL Error During Skia Deps Sync

This happens when git-sync-deps tries to download emsdk for WebAssembly.

**Solution:** emsdk is only needed for WebAssembly builds. Native builds work fine without it.

## File Structure

```
skia-renderer/
├── build/                    # Main project build
│   ├── skia-renderer(.exe)
│   └── compile_commands.json
├── build_deps/               # Dependency builds
├── deps/
│   ├── installed/            # Built dependencies
│   ├── SDL3/
│   ├── skia/
│   ├── vk-bootstrap/
│   ├── VulkanMemoryAllocator/
│   └── depot_tools/
├── sync.py                   # Download dependencies
├── build_deps.py             # Build dependencies
├── build.py                  # Build main project
└── build_all.py              # One-click build
```

## Clean Build

```bash
# Full clean rebuild
python build_all.py --clean

# Or manually
rm -rf build build_deps deps/installed
python build_deps.py --clean
python build.py --clean
```
