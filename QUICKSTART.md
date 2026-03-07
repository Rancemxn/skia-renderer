# Quick Start Guide

## Prerequisites

Install these tools before building:

```powershell
# Using winget
winget install Microsoft.VisualStudio.2022.Community
winget install Kitware.CMake
winget install aria2
winget install 7zip.7zip
winget install Git.Git

# Python 3.8+ (install from python.org or:
winget install Python.Python.3.12

# Vulkan SDK (download from vulkan.lunarg.com)
# Set environment variable:
setx VULKAN_SDK "C:\VulkanSDK\1.3.290.0"
```

## One-Click Build

```cmd
build_all.bat
```

This runs all three steps automatically.

## Step-by-Step Build

### 1. Download Dependencies

```cmd
# Standard download
sync_deps.bat

# Use Chinese mirrors (faster in China)
sync_deps.bat --mirror

# With proxy
sync_deps.bat --proxy http://127.0.0.1:7890

# Skip Skia dependencies (if git-sync-deps fails)
sync_deps.bat --skip-skia-deps

# Using Python script directly
python sync_deps.py --mirror --skip-skia-deps
```

**Options:**
- `--skip-skia` - Skip Skia
- `--skip-sdl` - Skip SDL3
- `--skip-vkbootstrap` - Skip vk-bootstrap
- `--skip-vma` - Skip VulkanMemoryAllocator
- `--skip-skia-deps` - Skip Skia internal dependencies
- `--mirror` - Use Chinese mirrors (ghp.ci)
- `--proxy URL` - Use proxy
- `--python PATH` - Python executable path

### 2. Build Dependencies

```cmd
# Default build
build_deps.bat

# With LLVM/Clang
build_deps.bat --skia-clang-win "C:/Program Files/LLVM"

# Enable Skia tools
build_deps.bat --skia-enable-tools true

# Debug build
build_deps.bat --build-type Debug

# Clean rebuild
build_deps.bat --clean

# Skip certain dependencies
build_deps.bat --skip-skia
```

**Skia Options:**
| Option | Default | Description |
|--------|---------|-------------|
| `--skia-target-cpu` | x64 | Target CPU (x64, x86, arm64) |
| `--skia-clang-win` | - | LLVM/Clang path |
| `--skia-official-build` | true | Optimized build |
| `--skia-debug` | false | Debug build |
| `--skia-enable-graphite` | true | Graphite backend |
| `--skia-enable-ganesh` | true | Ganesh backend |
| `--skia-use-vulkan` | true | Vulkan support |
| `--skia-use-gl` | true | OpenGL support |
| `--skia-enable-tools` | false | Build Skia tools |
| `--skia-enable-pdf` | true | PDF support |
| `--skia-use-angle` | false | ANGLE support |
| `--skia-use-libavif` | false | AVIF support |
| `--skia-use-freetype` | true | FreeType support |

### 3. Build Main Project

```cmd
# Default build
build_windows.bat

# Debug build
build_windows.bat --build-type Debug

# Clean rebuild
build_windows.bat --clean

# Custom dependency paths
build_windows.bat --vulkan-sdk "C:\VulkanSDK\1.3.290.0"

build_windows.bat --skia-path "C:\libs\skia" --sdl3-path "C:\libs\SDL3"
```

## Manual Skia Build

If you want full control over Skia build:

```cmd
cd deps\skia

# Set depot_tools in PATH
set PATH=%CD%\..\depot_tools;%PATH%

# Generate with custom args
bin\gn gen out/Release --ide=vs --sln="skia" --args="^
target_cpu=\"x64\" ^
clang_win=\"C:/Program Files/LLVM\" ^
is_official_build=true ^
is_debug=false ^
skia_enable_ganesh=true ^
skia_enable_graphite=true ^
skia_use_vulkan=true ^
skia_use_gl=true ^
skia_enable_tools=true ^
skia_enable_pdf=true ^
skia_use_system_expat=false ^
skia_use_system_harfbuzz=false ^
skia_use_system_icu=false ^
skia_use_system_zlib=false ^
extra_cflags_cc=[\"/GR\", \"/EHsc\"]"

# Build
ninja -C out/Release
```

## Troubleshooting

### SSL Error in emsdk Download

This error is common when building Skia:
```
Error: Downloading URL '...wasm-binaries.zip': SSL: UNEXPECTED_EOF_WHILE_READING
```

**Solution:** emsdk is only needed for WebAssembly builds. For native builds:

```cmd
# Skip Skia dependencies
sync_deps.bat --skip-skia-deps

# Or use fix script
fix_skia_deps.bat
```

### CMake Can't Find Vulkan

```cmd
# Set VULKAN_SDK environment variable
set VULKAN_SDK=C:\VulkanSDK\1.3.290.0

# Or pass directly
build_windows.bat --vulkan-sdk "C:\VulkanSDK\1.3.290.0"
```

### aria2c or 7z Not Found

```cmd
winget install aria2
winget install 7zip.7zip

# Or add to PATH manually
set PATH=C:\Program Files\7-Zip;%PATH%
```

### gn Not Found

```cmd
# depot_tools must be in PATH
set PATH=%CD%\deps\depot_tools;%PATH%

# Or set DEPOT_TOOLS
set DEPOT_TOOLS=C:\path\to\depot_tools
```

### Python Not Found

```cmd
# Install Python
winget install Python.Python.3.12

# Or specify path
sync_deps.bat --python "C:\Python312\python.exe"
build_deps.bat --python "C:\Python312\python.exe"
```

### Skia Build Fails with RTTI Errors

Make sure RTTI is enabled in GN args:
```
extra_cflags_cc=["/GR", "/EHsc"]
```

This is automatically included in build_deps.bat.

## File Structure After Build

```
skia-renderer/
├── build/
│   ├── Release/
│   │   └── skia-renderer.exe
│   └── compile_commands.json
├── build_deps/
│   ├── SDL3/
│   └── vk-bootstrap/
├── deps/
│   ├── installed/          # Built dependencies
│   │   ├── include/
│   │   └── lib/
│   ├── SDL3/
│   ├── skia/
│   │   └── out/Release/
│   │       └── skia.lib
│   ├── vk-bootstrap/
│   ├── VulkanMemoryAllocator/
│   └── depot_tools/
└── downloads/              # Downloaded archives (can delete)
```

## Clean Build

```cmd
# Full clean rebuild
build_all.bat --clean

# Or manually
rmdir /s /q build build_deps deps\installed
build_deps.bat --clean
build_windows.bat --clean
```
