# Skia Graphite Renderer

A high-performance 2D rendering application using **SDL3**, **Skia Graphite**, **vk-bootstrap**, **VMA**, and **Vulkan 1.3**.

## Features

- 🎨 Modern Vulkan 1.3 rendering backend
- ⚡ Skia Graphite for hardware-accelerated 2D graphics
- 🖼️ SDL3 for cross-platform window management
- 🔧 vk-bootstrap for easy Vulkan initialization
- 💾 VMA (Vulkan Memory Allocator) for efficient memory management
- 📦 C++20 with clean architecture
- 🛠️ Cross-platform Python build scripts
- 🚀 sccache support for fast rebuilds

## Quick Start

```bash
python build_all.py
```

See [QUICKSTART.md](QUICKSTART.md) for detailed options.

## Requirements

| Tool | Required | Notes |
|------|----------|-------|
| LLVM/Clang | ✅ | Required compiler |
| Ninja | ✅ | Build system |
| CMake | ✅ | Build configuration |
| Python | ✅ | 3.8+, for build scripts |
| Vulkan SDK | ✅ | Graphics API |
| Git | ✅ | For cloning repos |
| aria2 | ✅ | Fast parallel downloads |
| 7-Zip | ✅ | Archive extraction |
| sccache | ⭕ | Optional, speeds up rebuilds |

## Build Scripts

| Script | Description |
|--------|-------------|
| `build_all.py` | One-click complete build |
| `sync.py` | Download all dependencies |
| `build_deps.py` | Build SDL3, vk-bootstrap, Skia |
| `build.py` | Build main project |

## Usage

### One-Click Build
```bash
python build_all.py
```

### Step-by-Step
```bash
# 1. Download dependencies
python sync.py

# 2. Build dependencies
python build_deps.py --skia-tools

# 3. Build main project
python build.py
```

## Options

### sync.py
```
--proxy URL        Use proxy for downloads
--keep-downloads   Keep downloaded archives
--no-overwrite     Don't overwrite existing
```

### build_deps.py
```
--build-type TYPE  Release or Debug
--skia-tools       Build Skia tools
--clean            Clean before building
```

### build.py
```
--build-type TYPE  Release or Debug
--vulkan-sdk PATH  Vulkan SDK path
--clean            Clean before building
```

## sccache Support

sccache is automatically detected and used when available. It caches compilation results to speed up rebuilds.

**Install sccache:**
```bash
# Windows
winget install Mozilla.sccache

# Linux/macOS
cargo install sccache
```

**Configure (optional):**
```bash
# Local disk cache (default)
export SCCACHE_DIR=~/.cache/sccache
export SCCACHE_MAX_SIZE=10G

# S3 cache (for teams)
export SCCACHE_BUCKET=my-bucket
export AWS_ACCESS_KEY_ID=xxx
export AWS_SECRET_ACCESS_KEY=xxx
```

## Project Structure

```
skia-renderer/
├── src/
│   ├── main.cpp                 # Entry point
│   ├── core/
│   │   └── Application.h/cpp    # Main application
│   └── renderer/
│       ├── VulkanContext.h/cpp  # Vulkan (vk-bootstrap)
│       ├── Swapchain.h/cpp      # Swapchain management
│       └── SkiaRenderer.h/cpp   # Skia Graphite
├── deps/                        # Dependencies
├── sync.py                      # Download dependencies
├── build_deps.py                # Build dependencies
├── build.py                     # Build main project
└── build_all.py                 # One-click build
```

## Running

```bash
# Windows
build\skia-renderer.exe

# Linux/macOS
./build/skia-renderer

# Custom window size
build\skia-renderer.exe --width 1920 --height 1080
```

Press **ESC** to exit.

## Dependencies

| Library | Version | License |
|---------|---------|---------|
| SDL3 | 3.4.2+ | Zlib |
| vk-bootstrap | 1.4.343+ | MIT |
| VulkanMemoryAllocator | 3.3.0+ | MIT |
| Skia | Latest | BSD-3-Clause |

## License

MIT License
