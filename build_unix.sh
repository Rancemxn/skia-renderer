#!/bin/bash
# Build script for Linux/macOS
# Requires CMake 3.20+ and a C++20 compiler

set -e

echo "========================================"
echo "Skia Graphite Renderer - Unix Build"
echo "========================================"

# Check for cmake
if ! command -v cmake &> /dev/null; then
    echo "ERROR: CMake not found. Please install CMake 3.20+"
    exit 1
fi

# Check for Vulkan SDK
if [ -z "$VULKAN_SDK" ]; then
    echo "WARNING: VULKAN_SDK environment variable not set."
    echo "Please install Vulkan SDK from https://vulkan.lunarg.com/"
fi

# Detect number of cores
if command -v nproc &> /dev/null; then
    CORES=$(nproc)
elif command -v sysctl &> /dev/null; then
    CORES=$(sysctl -n hw.ncpu)
else
    CORES=4
fi

echo "Using $CORES parallel jobs"

# Create build directory
mkdir -p build
cd build

# Configure
echo ""
echo "Configuring..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
echo ""
echo "Building..."
cmake --build . -j"$CORES"

echo ""
echo "========================================"
echo "Build completed successfully!"
echo "Executable: build/skia-renderer"
echo "========================================"
