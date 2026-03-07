#!/usr/bin/env python3
"""
Skia Renderer - Build Main Project
Builds skia-renderer with LLVM/Clang or Visual Studio
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
    clang = find_tool("clang")
    if clang:
        clang_path = Path(clang)
        llvm_path = clang_path.parent.parent
        clang_pp = find_tool("clang++", [str(clang_path.parent)])
        return str(llvm_path), clang, clang_pp or str(clang_path.parent / "clang++")
    
    if platform.system() == "Windows":
        for base in [r"C:\Program Files\LLVM", r"C:\LLVM"]:
            llvm_path = Path(base)
            if (llvm_path / "bin" / "clang.exe").exists():
                return str(llvm_path), str(llvm_path / "bin" / "clang.exe"), str(llvm_path / "bin" / "clang++.exe")
    
    return None, None, None

def find_ninja(depot_tools: Path = None) -> str:
    """Find ninja executable"""
    ninja = find_tool("ninja")
    if ninja:
        return ninja
    
    if depot_tools and (depot_tools / "ninja.exe").exists():
        return str(depot_tools / "ninja.exe")
    
    llvm_path, _, _ = find_llvm()
    if llvm_path:
        ninja_path = Path(llvm_path) / "bin" / "ninja.exe"
        if ninja_path.exists():
            return str(ninja_path)
    
    return None

def find_visual_studio() -> str:
    """Find Visual Studio generator"""
    vs_paths = [
        r"C:\Program Files\Microsoft Visual Studio\2022\Community",
        r"C:\Program Files\Microsoft Visual Studio\2022\Professional",
        r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
    ]
    
    for p in vs_paths:
        if Path(p).exists():
            return "Visual Studio 17 2022"
    
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
    build_dir = script_dir / "build"
    install_dir = deps_dir / "installed"
    depot_tools = deps_dir / "depot_tools"
    
    print("=" * 50)
    print("Skia Graphite Renderer - Windows Build")
    print("=" * 50)
    print()
    
    # Detect tools
    print("[Detecting Build Tools]")
    
    cmake = find_tool("cmake")
    if not cmake:
        print("  ERROR: CMake not found")
        return 1
    print(f"  [OK] CMake: {cmake}")
    
    # Find LLVM
    llvm_path, clang, clang_pp = find_llvm()
    if llvm_path:
        print(f"  [OK] LLVM: {llvm_path}")
    
    # Determine generator
    use_llvm = args.llvm or (llvm_path is not None and not args.vs)
    
    if use_llvm:
        ninja = find_ninja(depot_tools)
        if ninja:
            print(f"  [OK] Ninja: {ninja}")
            generator = "Ninja"
        else:
            print("  WARNING: Ninja not found, trying Visual Studio")
            use_llvm = False
    
    if not use_llvm:
        vs_gen = find_visual_studio()
        if vs_gen:
            print(f"  [OK] Visual Studio: {vs_gen}")
            generator = vs_gen
        else:
            print("  ERROR: No suitable compiler found")
            return 1
    
    print()
    
    # Determine paths
    vulkan_sdk = args.vulkan_sdk or os.environ.get("VULKAN_SDK")
    sdl3_dir = args.sdl3_path or (install_dir / "lib" / "cmake" / "SDL3")
    if not sdl3_dir.exists():
        sdl3_dir = deps_dir / "SDL3"
    
    skia_dir = args.skia_path or deps_dir / "skia"
    vkb_dir = args.vkbootstrap_path or install_dir
    if not vkb_dir.exists():
        vkb_dir = deps_dir / "vk-bootstrap"
    
    vma_dir = args.vma_path or deps_dir / "VulkanMemoryAllocator"
    
    print("Configuration:")
    print(f"  Build Type: {args.build_type}")
    print(f"  Generator: {generator}")
    if llvm_path:
        print(f"  LLVM: {llvm_path}")
    print()
    
    print("Dependencies:")
    print(f"  Vulkan SDK: {vulkan_sdk or 'Not set'}")
    print(f"  SDL3: {sdl3_dir}")
    print(f"  Skia: {skia_dir}")
    print(f"  vk-bootstrap: {vkb_dir}")
    print(f"  VMA: {vma_dir}")
    print()
    
    # Clean if requested
    if args.clean and build_dir.exists():
        print("Cleaning build directory...")
        shutil.rmtree(build_dir)
    
    build_dir.mkdir(parents=True, exist_ok=True)
    
    # Configure
    print("Configuring project...")
    print()
    
    cmd = ["cmake", "-S", str(script_dir), "-B", str(build_dir), "-G", generator]
    
    if generator == "Ninja":
        cmd.append(f"-DCMAKE_BUILD_TYPE={args.build_type}")
    
    if llvm_path and clang and clang_pp:
        cmd.extend([
            f"-DCMAKE_C_COMPILER={clang}",
            f"-DCMAKE_CXX_COMPILER={clang_pp}",
        ])
    
    cmd.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
    
    if vulkan_sdk:
        cmd.append(f"-DVULKAN_SDK={vulkan_sdk}")
        cmd.append(f"-DCMAKE_PREFIX_PATH={vulkan_sdk}")
    
    cmd.append(f"-DSDL3_DIR={sdl3_dir}")
    cmd.append(f"-DSKIA_DIR={skia_dir}")
    cmd.append(f"-DVKBOOTSTRAP_DIR={vkb_dir}")
    cmd.append(f"-DVMA_DIR={vma_dir}")
    
    if args.cmake_args:
        cmd.extend(args.cmake_args.split())
    
    try:
        run_cmd(cmd)
    except Exception as e:
        print(f"\nERROR: CMake configuration failed: {e}")
        print("\nTroubleshooting:")
        print("  1. Set VULKAN_SDK environment variable")
        print("  2. Run sync_deps.py and build_deps.py first")
        print("  3. Use --vulkan-sdk, --sdl3-path options")
        return 1
    
    # Build
    print()
    print(f"Building {args.build_type}...")
    print()
    
    try:
        run_cmd(["cmake", "--build", str(build_dir), "--config", args.build_type])
    except Exception as e:
        print(f"\nERROR: Build failed: {e}")
        return 1
    
    # Success
    print()
    print("=" * 50)
    print("Build Complete!")
    print("=" * 50)
    print()
    
    if generator == "Ninja":
        exe_path = build_dir / "skia-renderer.exe"
    else:
        exe_path = build_dir / args.build_type / "skia-renderer.exe"
    
    if exe_path.exists():
        print(f"Executable: {exe_path}")
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
    
    # Compiler options
    parser.add_argument("--llvm", action="store_true", help="Use LLVM/Clang + Ninja")
    parser.add_argument("--vs", action="store_true", help="Use Visual Studio")
    
    # Custom paths
    parser.add_argument("--vulkan-sdk", help="Vulkan SDK path")
    parser.add_argument("--sdl3-path", help="SDL3 path")
    parser.add_argument("--skia-path", help="Skia path")
    parser.add_argument("--vkbootstrap-path", help="vk-bootstrap path")
    parser.add_argument("--vma-path", help="VulkanMemoryAllocator path")
    parser.add_argument("--cmake-args", help="Extra CMake arguments")
    
    args = parser.parse_args()
    
    try:
        return build_project(args)
    except Exception as e:
        print(f"\nERROR: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
