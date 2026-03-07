# Skia Graphite Renderer

A high-performance 2D rendering application using **SDL3**, **Skia Graphite**, **vk-bootstrap**, **VMA**, and **Vulkan 1.3**.

## Features

- Modern Vulkan 1.3 rendering backend
- Skia Graphite for hardware-accelerated 2D graphics
- SDL3 for cross-platform window management
- vk-bootstrap for easy Vulkan initialization
- VMA (Vulkan Memory Allocator) for efficient memory management
- C++20 with clean architecture

## Requirements

- CMake 3.20+
- C++20 compiler (LLVM Clang 22+, GCC 12+, or MSVC 2022+)
- Vulkan SDK 1.3+
- Git

## Dependencies

The project requires the following libraries to be built and placed in the `deps/` directory:

| Library | Version | Path |
|---------|---------|------|
| SDL3 | 3.4.2+ | `deps/SDL3/` |
| vk-bootstrap | 1.4.343+ | `deps/vk-bootstrap/` |
| VulkanMemoryAllocator | 3.3.0+ | `deps/VulkanMemoryAllocator/` |
| Skia | chrome/m146 | `deps/skia/` |

### Building Dependencies

#### SDL3
```bash
git clone --depth 1 --branch release-3.4.2 https://github.com/libsdl-org/SDL.git deps/SDL3-src
cd deps/SDL3-src
mkdir build && cd build
cmake .. -DSDL_X11_XTEST=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
cmake --install . --prefix ../SDL3
```

#### vk-bootstrap
```bash
git clone --depth 1 --branch v1.4.343 https://github.com/charles-lunarg/vk-bootstrap.git deps/vk-bootstrap-src
cd deps/vk-bootstrap-src
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
# Copy include and lib to deps/vk-bootstrap/
```

#### VulkanMemoryAllocator (Header-only)
```bash
git clone --depth 1 --branch v3.3.0 https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git deps/VulkanMemoryAllocator
```

#### Skia
```bash
# Install depot_tools first
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="${PWD}/depot_tools:$PATH"

# Clone Skia
git clone https://skia.googlesource.com/skia.git deps/skia
cd deps/skia
git checkout chrome/m146
python3 tools/git-sync-deps

# Build
bin/gn gen out/Release --args='
is_official_build=true
skia_enable_tools=false
skia_use_system_expat=false
skia_use_system_harfbuzz=false
skia_use_system_icu=false
skia_use_system_libjpeg_turbo=false
skia_use_system_libpng=false
skia_use_system_libwebp=false
skia_use_system_zlib=false
skia_enable_gpu=true
skia_enable_vulkan=true
skia_use_vulkan=true
extra_cflags_cc=["-frtti"]
'
ninja -C out/Release
```

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

## Running

```bash
./skia-renderer
./skia-renderer --width 1920 --height 1080
```

Press **ESC** to exit.

## Architecture

```
src/
├── main.cpp                 # Entry point
├── core/
│   ├── Application.h/cpp    # Main application class
└── renderer/
    ├── VulkanContext.h/cpp  # Vulkan initialization (vk-bootstrap)
    ├── Swapchain.h/cpp      # Swapchain management
    └── SkiaRenderer.h/cpp   # Skia Graphite rendering
```

## License

MIT License
