"""
Skia Renderer - Build Main Project
Builds skia-renderer with LLVM/Clang + Ninja + sccache

Build outputs are separated by build type:
- Debug:   build/Debug/
- Release: build/Release/
"""

import os
import sys
import shutil
import subprocess
import argparse
import platform
from pathlib import Path

def find_tool(name: str, extra_paths: list = None) -> str:
    """Find a tool in PATH or specified paths"""
    result = shutil.which(name)
    if result:
        return result
    
    if extra_paths:
        for path in extra_paths:
            exe = Path(path) / name
            if exe.exists():
                return str(exe)
            if platform.system() == "Windows":
                exe = Path(path) / f"{name}.exe"
                if exe.exists():
                    return str(exe)
    
    return None

def find_llvm() -> tuple:
    """Find LLVM installation"""
    # Check PATH first
    clang = find_tool("clang")
    if clang:
        clang_path = Path(clang)
        llvm_path = clang_path.parent.parent
        clang_pp = find_tool("clang++", [str(clang_path.parent)])
        return str(llvm_path), clang, clang_pp or str(clang_path.parent / "clang++")
    
    # Check common Windows paths
    if platform.system() == "Windows":
        for base in [r"C:\Program Files\LLVM", r"C:\LLVM"]:
            llvm_path = Path(base)
            if (llvm_path / "bin" / "clang.exe").exists():
                return str(llvm_path), str(llvm_path / "bin" / "clang.exe"), str(llvm_path / "bin" / "clang++.exe")
    
    # Check common Unix paths
    for base in ["/usr", "/usr/local", "/opt/homebrew"]:
        llvm_path = Path(base)
        if (llvm_path / "bin" / "clang").exists():
            return str(llvm_path), str(llvm_path / "bin" / "clang"), str(llvm_path / "bin" / "clang++")
    
    return None, None, None

def find_ninja(depot_tools: Path = None) -> str:
    """Find ninja executable"""
    ninja = find_tool("ninja")
    if ninja:
        return ninja
    
    # Check depot_tools
    if depot_tools:
        ninja_exe = "ninja.exe" if platform.system() == "Windows" else "ninja"
        if (depot_tools / ninja_exe).exists():
            return str(depot_tools / ninja_exe)
    
    # Check LLVM bin
    llvm_path, _, _ = find_llvm()
    if llvm_path:
        ninja_exe = "ninja.exe" if platform.system() == "Windows" else "ninja"
        ninja_path = Path(llvm_path) / "bin" / ninja_exe
        if ninja_path.exists():
            return str(ninja_path)
    
    return None

def find_sccache() -> str:
    """Find sccache executable"""
    sccache = find_tool("sccache")
    if sccache:
        return sccache
    
    # Check common paths
    if platform.system() == "Windows":
        for base in [r"C:\Program Files\sccache", r"C:\sccache", os.path.expanduser("~\\.cargo\\bin")]:
            exe = Path(base) / "sccache.exe"
            if exe.exists():
                return str(exe)
    else:
        for base in ["/usr/local/bin", os.path.expanduser("~/.cargo/bin")]:
            exe = Path(base) / "sccache"
            if exe.exists():
                return str(exe)
    
    return None

def run_cmd(cmd: list, cwd: str = None, check: bool = True, env: dict = None) -> subprocess.CompletedProcess:
    """Run a command"""
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    
    print(f"  Running: {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=cwd, env=merged_env)
    if check and result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, cmd)
    return result

def build_project(args):
    """Main build function"""
    script_dir = Path(__file__).parent.resolve()
    deps_dir = script_dir / "deps"
    depot_tools = deps_dir / "depot_tools"
    
    build_type = args.build_type
    
    # Build-type-specific directories
    build_dir = script_dir / "build" / build_type
    install_dir = deps_dir / "installed" / build_type
    
    print("=" * 60)
    print("Skia Graphite Renderer - Build")
    print("=" * 60)
    print()
    
    # Detect tools
    print("[Detecting Build Tools]")
    
    cmake = find_tool("cmake")
    if not cmake:
        print("  ERROR: CMake not found")
        return 1
    print(f"  [OK] CMake: {cmake}")
    
    # Find LLVM (required)
    llvm_path, clang, clang_pp = find_llvm()
    if not llvm_path:
        print("  ERROR: LLVM/Clang not found")
        print("  Install LLVM: winget install LLVM.LLVM (Windows)")
        print("  Or: apt install clang (Linux)")
        return 1
    print(f"  [OK] LLVM: {llvm_path}")
    
    ninja = find_ninja(depot_tools)
    if not ninja:
        print("  ERROR: Ninja not found")
        return 1
    print(f"  [OK] Ninja: {ninja}")
    
    # Find sccache (optional)
    sccache = find_sccache()
    if sccache:
        print(f"  [OK] sccache: {sccache}")
    else:
        print("  [INFO] sccache not found (optional, speeds up rebuilds)")
    
    print()
    
    # Determine paths
    vulkan_sdk = args.vulkan_sdk or os.environ.get("VULKAN_SDK")
    
    # SDL3 path - build-type-specific
    sdl3_dir = args.sdl3_path
    if not sdl3_dir:
        # Check build-type-specific install first
        sdl3_cmake = install_dir / "lib" / "cmake" / "SDL3"
        if sdl3_cmake.exists():
            sdl3_dir = sdl3_cmake
        else:
            # Fallback to deps/SDL3
            sdl3_dir = deps_dir / "SDL3"
    
    # Skia path - build-type-specific
    skia_dir = args.skia_path or deps_dir / "skia"
    
    # vk-bootstrap path - build-type-specific
    vkb_dir = args.vkbootstrap_path or install_dir
    
    print("Configuration:")
    print(f"  Build Type: {build_type}")
    print(f"  Build Dir:  {build_dir}")
    print(f"  Generator:  Ninja (LLVM/Clang)")
    if sccache:
        print(f"  Cache:      sccache")
    print()
    
    print("Dependencies:")
    print(f"  Vulkan SDK:   {vulkan_sdk or 'Not set'}")
    print(f"  SDL3:         {sdl3_dir}")
    print(f"  Skia:         {skia_dir} (out/{build_type})")
    print(f"  vk-bootstrap: {vkb_dir}")
    print("  VMA:          (built into Skia)")
    print()
    
    # Clean if requested
    if args.clean and build_dir.exists():
        print(f"Cleaning build directory: {build_dir}")
        shutil.rmtree(build_dir)
    
    build_dir.mkdir(parents=True, exist_ok=True)
    
    # Configure
    print("Configuring project...")
    print()
    
    cmd = [
        "cmake", "-S", str(script_dir), "-B", str(build_dir),
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_C_COMPILER={clang}",
        f"-DCMAKE_CXX_COMPILER={clang_pp}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    ]
    
    # Add sccache as compiler launcher
    if sccache:
        cmd.append(f"-DCMAKE_C_COMPILER_LAUNCHER={sccache}")
        cmd.append(f"-DCMAKE_CXX_COMPILER_LAUNCHER={sccache}")
    
    if vulkan_sdk:
        cmd.append(f"-DVULKAN_SDK={vulkan_sdk}")
        cmd.append(f"-DCMAKE_PREFIX_PATH={vulkan_sdk}")
    
    cmd.append(f"-DSDL3_DIR={sdl3_dir}")
    cmd.append(f"-DSKIA_DIR={skia_dir}")
    cmd.append(f"-DVKBOOTSTRAP_DIR={vkb_dir}")
    cmd.append(f"-DCMAKE_INSTALL_PREFIX={install_dir}")
    
    if args.cmake_args:
        cmd.extend(args.cmake_args.split())
    
    try:
        run_cmd(cmd)
    except Exception as e:
        print(f"\nERROR: CMake configuration failed: {e}")
        print("\nTroubleshooting:")
        print("  1. Set VULKAN_SDK environment variable")
        print(f"  2. Run: python build_deps.py --build-type {build_type}")
        print(f"  3. Run: python sync.py")
        return 1
    
    # Build
    print()
    print(f"Building {build_type}...")
    print()
    
    try:
        run_cmd(["cmake", "--build", str(build_dir), "--config", build_type])
    except Exception as e:
        print(f"\nERROR: Build failed: {e}")
        return 1
    
    # Success
    print()
    print("=" * 60)
    print("Build Complete!")
    print("=" * 60)
    print()
    
    exe_name = "skia-renderer.exe" if platform.system() == "Windows" else "skia-renderer"
    exe_path = build_dir / exe_name
    
    if exe_path.exists():
        size_mb = exe_path.stat().st_size / (1024 * 1024)
        print(f"Executable: {exe_path}")
        print(f"Size: {size_mb:.1f} MB")
        print(f"\nRun: {exe_path}")
    else:
        print(f"Executable should be in: {build_dir}")
    
    print()
    
    return 0

def main():
    parser = argparse.ArgumentParser(description="Build Skia Graphite Renderer")
    
    # Build options
    parser.add_argument("--build-type", default="Release", choices=["Release", "Debug"],
                       help="Build type (default: Release)")
    parser.add_argument("--clean", action="store_true", help="Clean before building")
    
    # Custom paths
    parser.add_argument("--vulkan-sdk", help="Vulkan SDK path")
    parser.add_argument("--sdl3-path", help="SDL3 CMake path (auto-detected if not set)")
    parser.add_argument("--skia-path", help="Skia path")
    parser.add_argument("--vkbootstrap-path", help="vk-bootstrap path")
    parser.add_argument("--cmake-args", help="Extra CMake arguments")
    
    args = parser.parse_args()
    
    try:
        return build_project(args)
    except Exception as e:
        print(f"\nERROR: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
