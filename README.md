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
python build_all.py
```

See [QUICKSTART.md](QUICKSTART.md) for detailed options.

## Requirements

| Tool | Version | Notes |
|------|---------|-------|
| LLVM/Clang | Latest | Required compiler |
| Ninja | Latest | Build system (included with LLVM) |
| CMake | 3.20+ | Build configuration |
| Python | 3.8+ | Build scripts |
| Vulkan SDK | 1.3+ | Graphics API |
| Git | Latest | For cloning repos |
| aria2 | Latest | Fast parallel downloads |
| 7-Zip | Latest | Archive extraction |

## Build Scripts

| Script | Description |
|--------|-------------|
| `build_all.py` | One-click complete build |
| `sync.py` | Download all dependencies |
| `build_deps.py` | Build SDL3, vk-bootstrap, Skia |
| `build.py` | Build main project |

## Usage

### Download Dependencies
```bash
python sync.py
```

### Build Dependencies
```bash
python build_deps.py --skia-tools
```

### Build Main Project
```bash
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
