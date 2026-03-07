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

## Quick Start

```bash
# One-click build
python build_all.py --llvm
```

See [QUICKSTART.md](QUICKSTART.md) for detailed options.

## Requirements

| Tool | Version | Notes |
|------|---------|-------|
| CMake | 3.20+ | Build system |
| Python | 3.8+ | Build scripts |
| Vulkan SDK | 1.3+ | Graphics API |
| Git | Latest | For cloning repos |
| aria2 | Latest | Fast parallel downloads |
| 7-Zip | Latest | Archive extraction |

**Compiler (choose one):**
- LLVM/Clang + Ninja (recommended)
- Visual Studio 2022 (Windows)
- GCC/Clang (Linux/macOS)

## Build Scripts

| Script | Description |
|--------|-------------|
| `build_all.py` | One-click complete build |
| `sync_deps.py` | Download all dependencies |
| `build_deps.py` | Build SDL3, vk-bootstrap, Skia |
| `build_windows.py` | Build main project |

## Usage

### Download Dependencies
```bash
python sync_deps.py --mirror --skip-skia-deps
```

### Build Dependencies
```bash
python build_deps.py --llvm --skia-tools
```

### Build Main Project
```bash
python build_windows.py --llvm
```

## Options

### sync_deps.py
```
--mirror           Use Chinese mirrors
--proxy URL        Use proxy for downloads
--skip-skia-deps   Skip Skia dependencies
--keep-downloads   Keep downloaded archives
```

### build_deps.py
```
--llvm             Use LLVM/Clang + Ninja
--vs               Use Visual Studio
--build-type TYPE  Release or Debug
--skia-tools       Build Skia tools
--clean            Clean before building
```

### build_windows.py
```
--llvm             Use LLVM/Clang + Ninja
--vs               Use Visual Studio
--build-type TYPE  Release or Debug
--vulkan-sdk PATH  Vulkan SDK path
--clean            Clean before building
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
├── sync_deps.py                 # Download dependencies
├── build_deps.py                # Build dependencies
├── build_windows.py             # Build main project
└── build_all.py                 # One-click build
```

## Running

```bash
# Windows
build\skia-renderer.exe

# Linux
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
