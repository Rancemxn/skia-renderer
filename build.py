"""
Skia Renderer - Build System
Unified build script for dependencies and main project

Directory Structure:
  deps/                           - Source + build outputs
    ├── SDL3/out/{Debug,Release}/
    ├── vk-bootstrap/out/{Debug,Release}/
    ├── skia/out/{Debug,Release}/
    └── depot_tools/

  build/                          - Main project outputs
    ├── Debug/
    └── Release/
"""

import os
import sys
import shutil
import subprocess
import argparse
import stat
import platform
from pathlib import Path

# ========================================
# Tool Finding
# ========================================

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
    
    if depot_tools:
        ninja_exe = "ninja.exe" if platform.system() == "Windows" else "ninja"
        if (depot_tools / ninja_exe).exists():
            return str(depot_tools / ninja_exe)
    
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

def find_gn(depot_tools: Path) -> str:
    """Find gn executable"""
    gn = find_tool("gn", [str(depot_tools)])
    return gn

# ========================================
# Utility Functions
# ========================================

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

def remove_readonly(func, path, _):
    os.chmod(path, stat.S_IWRITE)
    func(path)

# ========================================
# Build Functions
# ========================================

def build_sdl3(source_dir: Path, build_type: str, 
               clang: str, clang_pp: str, sccache: str = None) -> bool:
    """Build SDL3 with CMake -> deps/SDL3/out/{Debug,Release}/"""
    print("\n[SDL3]")
    
    if not source_dir.exists():
        print(f"  ERROR: SDL3 source not found: {source_dir}")
        print("  Run: python sync.py")
        return False
    
    # Check if prebuilt
    if not (source_dir / "CMakeLists.txt").exists():
        print("  SDL3 appears to be prebuilt, skipping")
        return True
    
    # Build directory: deps/SDL3/out/{Debug,Release}
    build_dir = source_dir / "out" / build_type
    
    # CMake configure
    cmd = [
        "cmake", "-S", str(source_dir), "-B", str(build_dir), 
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_C_COMPILER={clang}",
        f"-DCMAKE_CXX_COMPILER={clang_pp}",
        f"-DCMAKE_INSTALL_PREFIX={build_dir}",
        "-DSDL_VULKAN=ON", "-DSDL_OPENGL=ON", 
        "-DSDL_TEST=OFF", "-DSDL_TESTS=OFF"
    ]
    
    if platform.system() == "Windows":
        runtime_lib = "MultiThreaded" if build_type == "Release" else "MultiThreadedDebug"
        cmd.append(f"-DCMAKE_MSVC_RUNTIME_LIBRARY={runtime_lib}")
    
    if sccache:
        cmd.append(f"-DCMAKE_C_COMPILER_LAUNCHER={sccache}")
        cmd.append(f"-DCMAKE_CXX_COMPILER_LAUNCHER={sccache}")
    
    print("  Configuring...")
    run_cmd(cmd)
    
    print("  Building...")
    run_cmd(["cmake", "--build", str(build_dir), "--config", build_type])
    
    print("  Installing...")
    run_cmd(["cmake", "--install", str(build_dir), "--config", build_type])
    
    print(f"  Output: {build_dir}")
    return True

def build_vkbootstrap(source_dir: Path, build_type: str,
                      clang: str, clang_pp: str, sccache: str = None) -> bool:
    """Build vk-bootstrap with CMake -> deps/vk-bootstrap/out/{Debug,Release}/"""
    print("\n[vk-bootstrap]")
    
    if not source_dir.exists():
        print(f"  ERROR: vk-bootstrap source not found: {source_dir}")
        print("  Run: python sync.py")
        return False
    
    # Build directory: deps/vk-bootstrap/out/{Debug,Release}
    build_dir = source_dir / "out" / build_type
    
    # CMake configure
    cmd = [
        "cmake", "-S", str(source_dir), "-B", str(build_dir), 
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_C_COMPILER={clang}",
        f"-DCMAKE_CXX_COMPILER={clang_pp}",
        f"-DCMAKE_INSTALL_PREFIX={build_dir}"
    ]
    
    if platform.system() == "Windows":
        runtime_lib = "MultiThreaded" if build_type == "Release" else "MultiThreadedDebug"
        cmd.append(f"-DCMAKE_MSVC_RUNTIME_LIBRARY={runtime_lib}")
    
    if sccache:
        cmd.append(f"-DCMAKE_C_COMPILER_LAUNCHER={sccache}")
        cmd.append(f"-DCMAKE_CXX_COMPILER_LAUNCHER={sccache}")
    
    print("  Configuring...")
    run_cmd(cmd)
    
    print("  Building...")
    run_cmd(["cmake", "--build", str(build_dir), "--config", build_type])
    
    print("  Installing...")
    run_cmd(["cmake", "--install", str(build_dir), "--config", build_type])
    
    print(f"  Output: {build_dir}")
    return True

def build_skia(skia_dir: Path, build_type: str, llvm_path: str,
               depot_tools: Path, target_cpu: str, sccache: str = None) -> bool:
    """Build Skia with GN + Ninja -> deps/skia/out/{Debug,Release}/"""
    print("\n[Skia]")
    
    if not skia_dir.exists():
        print(f"  ERROR: Skia source not found: {skia_dir}")
        print("  Run: python sync.py")
        return False
    
    # Find tools
    gn = find_gn(depot_tools)
    if not gn:
        print("  ERROR: gn not found in depot_tools")
        return False
    
    ninja = find_ninja(depot_tools)
    if not ninja:
        print("  ERROR: ninja not found")
        return False
    
    # Apply patches
    script_dir = Path(__file__).parent.resolve()
    patches_dir = script_dir / "patches"
    if patches_dir.exists():
        for patch_file in patches_dir.glob("*.patch"):
            result = subprocess.run(
                ["git", "apply", "--check", str(patch_file)],
                cwd=str(skia_dir), capture_output=True
            )
            if result.returncode == 0:
                subprocess.run(["git", "apply", str(patch_file)], cwd=str(skia_dir), check=True)
                print(f"  Applied patch: {patch_file.name}")
            else:
                result = subprocess.run(
                    ["git", "apply", "--reverse", "--check", str(patch_file)],
                    cwd=str(skia_dir), capture_output=True
                )
                if result.returncode == 0:
                    print(f"  Patch already applied: {patch_file.name}")
    
    # Setup environment
    env = os.environ.copy()
    env["PATH"] = str(depot_tools) + os.pathsep + env.get("PATH", "")
    if sccache:
        env["SCCACHE_DIR"] = str(skia_dir / ".sccache")
    
    # GN args
    is_windows = platform.system() == "Windows"
    crt_flag = "/MTd" if build_type == "Debug" else "/MT"
    
    gn_args = [
        f'target_cpu="{target_cpu}"',
        'cc="clang"', 'cxx="clang++"',
        f'is_official_build={"true" if build_type == "Release" else "false"}',
        f'is_debug={"true" if build_type == "Debug" else "false"}',
        'is_component_build=false',
        'skia_enable_ganesh=true',
        'skia_enable_graphite=true',
        'skia_use_vulkan=true',
        'skia_use_gl=true',
        'skia_enable_pdf=true',
        'skia_enable_precompile=true',
        'skia_use_angle=true',
        'skia_use_freetype=true',
        'skia_use_expat=true',
        'skia_use_zlib=true',
        'skia_use_wuffs=true',
        'skia_use_vma=true',
        # Use bundled libraries
        'skia_use_system_expat=false',
        'skia_use_system_harfbuzz=false',
        'skia_use_system_icu=false',
        'skia_use_system_libjpeg_turbo=false',
        'skia_use_system_libpng=false',
        'skia_use_system_libwebp=false',
        'skia_use_system_zlib=false',
        'skia_use_system_freetype2=false',
        'skia_enable_tools=false',
        "skia_use_libavif=true",
        "skia_use_libwebp_encode=true",
        "skia_use_libwebp_decode=true",
        "skia_use_libpng_encode=true",
        "skia_use_libpng_decode=true",
        "skia_use_libjpeg_turbo_encode=true",
        "skia_use_libjpeg_turbo_decode=true",
        "skia_use_libjxl_decode=true",
    ]
    
    if is_windows:
        gn_args.append(f'clang_win="{llvm_path}"')
        gn_args.append(f'extra_cflags=["{crt_flag}"]')
        gn_args.append(f'extra_cflags_cc=["/GR", "/EHsc", "{crt_flag}"]')
    else:
        gn_args.append('extra_cflags_cc=["-frtti", "-fexceptions"]')
    
    if sccache:
        gn_args.append(f'cc_wrapper="{sccache}"')
    
    gn_args_str = " ".join(gn_args)
    
    # Build directory: deps/skia/out/{Debug,Release}
    out_dir = f"out/{build_type}"
    print(f"  Configuring...")
    run_cmd([gn, "gen", out_dir, f"--args={gn_args_str}"], cwd=str(skia_dir), env=env)
    
    print("  Building...")
    run_cmd([ninja, "-C", out_dir], cwd=str(skia_dir), env=env)
    
    print(f"  Output: {skia_dir / out_dir}")
    return True

def check_sdl3_built(sdl3_out_dir: Path) -> tuple:
    """Check if SDL3 has been built and find SDL3Config.cmake location"""
    # Possible locations for SDL3Config.cmake
    possible_paths = [
        sdl3_out_dir / "lib" / "cmake" / "SDL3" / "SDL3Config.cmake",
        sdl3_out_dir / "cmake" / "SDL3Config.cmake",
        sdl3_out_dir / "SDL3Config.cmake",
        sdl3_out_dir / "lib" / "cmake" / "SDL3Config.cmake",
    ]
    
    for path in possible_paths:
        if path.exists():
            return True, path.parent
    
    return False, None

def build_main_project(script_dir: Path, build_type: str,
                       clang: str, clang_pp: str, sccache: str = None,
                       vulkan_sdk: str = None, clean: bool = False,
                       cmake_args: str = None) -> bool:
    """Build main project with CMake -> build/{Debug,Release}/"""
    print("\n[skia-renderer]")
    
    deps_dir = script_dir / "deps"
    
    # Build directory: build/{Debug,Release}
    build_dir = script_dir / "build" / build_type
    
    # Clean if requested
    if clean and build_dir.exists():
        print(f"  Cleaning: {build_dir}")
        shutil.rmtree(build_dir, onerror=remove_readonly)
    
    build_dir.mkdir(parents=True, exist_ok=True)
    
    # Determine dependency paths
    sdl3_out_dir = deps_dir / "SDL3" / "out" / build_type
    vkb_dir = deps_dir / "vk-bootstrap" / "out" / build_type
    skia_dir = deps_dir / "skia"
    spdlog_dir = deps_dir / "spdlog"
    cli11_dir = deps_dir / "CLI11"
    
    # Check if SDL3 is built
    print("  Checking dependencies...")
    sdl3_built, sdl3_config_dir = check_sdl3_built(sdl3_out_dir)
    if not sdl3_built:
        print(f"  WARNING: SDL3 not found at {sdl3_out_dir}")
        print(f"  Run: python build.py --build-type {build_type} (without --skip-deps)")
    else:
        print(f"  SDL3 found: {sdl3_config_dir}")
    
    # Check other dependencies
    skia_lib = skia_dir / "out" / build_type / "skia.lib"
    skia_lib_alt = skia_dir / "out" / build_type / "libskia.a"
    if not skia_lib.exists() and not skia_lib_alt.exists():
        print(f"  WARNING: Skia not built for {build_type}")
        skia_found = False
    else:
        print(f"  Skia found: {skia_lib if skia_lib.exists() else skia_lib_alt}")
        skia_found = True
    
    if not (spdlog_dir / "include" / "spdlog" / "spdlog.h").exists():
        print(f"  WARNING: spdlog not found")
    
    if not (cli11_dir / "include" / "CLI" / "CLI.hpp").exists():
        print(f"  WARNING: CLI11 not found")
    
    # CMake configure
    cmd = [
        "cmake", "-S", str(script_dir), "-B", str(build_dir),
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_C_COMPILER={clang}",
        f"-DCMAKE_CXX_COMPILER={clang_pp}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    ]
    
    if sccache:
        cmd.extend([
            f"-DCMAKE_C_COMPILER_LAUNCHER={sccache}",
            f"-DCMAKE_CXX_COMPILER_LAUNCHER={sccache}"
        ])
    
    if vulkan_sdk:
        cmd.extend([f"-DVULKAN_SDK={vulkan_sdk}", f"-DCMAKE_PREFIX_PATH={vulkan_sdk}"])
    
    # Use SDL3 config directory if found, otherwise use output directory
    sdl3_cmake_dir = sdl3_config_dir if sdl3_built else sdl3_out_dir
    
    cmd.extend([
        f"-DSDL3_DIR={sdl3_cmake_dir}",
        f"-DVKBOOTSTRAP_DIR={vkb_dir}",
        f"-DSKIA_DIR={skia_dir}",
        f"-DSPDLOG_DIR={spdlog_dir}",
        f"-DCLI11_DIR={cli11_dir}"
    ])
    
    # Print CMake variables for debugging
    print(f"  CMake variables:")
    print(f"    SDL3_DIR={sdl3_cmake_dir}")
    print(f"    VKBOOTSTRAP_DIR={vkb_dir}")
    print(f"    SKIA_DIR={skia_dir}")
    print(f"    SPDLOG_DIR={spdlog_dir}")
    print(f"    CLI11_DIR={cli11_dir}")
    
    if cmake_args:
        cmd.extend(cmake_args.split())
    
    print("  Configuring...")
    run_cmd(cmd)
    
    print("  Building...")
    run_cmd(["cmake", "--build", str(build_dir), "--config", build_type])
    
    # Report output
    exe_name = "skia-renderer.exe" if platform.system() == "Windows" else "skia-renderer"
    exe_path = build_dir / exe_name
    if exe_path.exists():
        size_mb = exe_path.stat().st_size / (1024 * 1024)
        print(f"  Executable: {exe_path} ({size_mb:.1f} MB)")
    
    return True

# ========================================
# Main
# ========================================

def main():
    parser = argparse.ArgumentParser(
        description="Skia Renderer Build System",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python build.py                          # Build Release (dependencies + main)
  python build.py --build-type Debug       # Build Debug
  python build.py --skip-deps              # Build main project only
  python build.py --skip-main              # Build dependencies only
  python build.py --clean                  # Clean rebuild

Directory Structure:
  deps/SDL3/out/{Debug,Release}/           # SDL3 builds
  deps/vk-bootstrap/out/{Debug,Release}/   # vk-bootstrap builds
  deps/skia/out/{Debug,Release}/           # Skia builds
  build/{Debug,Release}/                   # Main project builds
        """
    )
    
    # Build options
    parser.add_argument("--build-type", default="Release", choices=["Release", "Debug"],
                       help="Build type (default: Release)")
    parser.add_argument("--target-cpu", default="x64", help="Target CPU for Skia (default: x64)")
    parser.add_argument("--clean", action="store_true", help="Clean before building")
    
    # Skip options
    parser.add_argument("--skip-deps", action="store_true", help="Skip building dependencies")
    parser.add_argument("--skip-main", action="store_true", help="Skip building main project")
    parser.add_argument("--skip-sdl", action="store_true", help="Skip SDL3")
    parser.add_argument("--skip-vkbootstrap", action="store_true", help="Skip vk-bootstrap")
    parser.add_argument("--skip-skia", action="store_true", help="Skip Skia")
    
    # Other options
    parser.add_argument("--vulkan-sdk", help="Vulkan SDK path")
    parser.add_argument("--cmake-args", help="Extra CMake arguments for main project")
    
    args = parser.parse_args()
    
    # Paths
    script_dir = Path(__file__).parent.resolve()
    deps_dir = script_dir / "deps"
    depot_tools = deps_dir / "depot_tools"
    
    print("=" * 60)
    print("Skia Renderer Build System")
    print("=" * 60)
    print(f"Build Type: {args.build_type}")
    print()
    
    # Find tools
    print("[Detecting Tools]")
    
    cmake = find_tool("cmake")
    if not cmake:
        print("  ERROR: CMake not found")
        return 1
    print(f"  CMake: {cmake}")
    
    llvm_path, clang, clang_pp = find_llvm()
    if not llvm_path:
        print("  ERROR: LLVM/Clang not found")
        return 1
    print(f"  LLVM: {llvm_path}")
    
    ninja = find_ninja(depot_tools)
    if not ninja:
        print("  ERROR: Ninja not found")
        return 1
    print(f"  Ninja: {ninja}")
    
    sccache = find_sccache()
    if sccache:
        print(f"  sccache: {sccache}")
    else:
        print("  sccache: not found (optional)")
    
    print()
    
    vulkan_sdk = args.vulkan_sdk or os.environ.get("VULKAN_SDK")
    
    # Build dependencies
    if not args.skip_deps:
        print("=" * 60)
        print("Building Dependencies")
        print("=" * 60)
        
        if not args.skip_sdl:
            build_sdl3(deps_dir / "SDL3", args.build_type, clang, clang_pp, sccache)
        
        if not args.skip_vkbootstrap:
            build_vkbootstrap(deps_dir / "vk-bootstrap", args.build_type, clang, clang_pp, sccache)
        
        if not args.skip_skia:
            build_skia(deps_dir / "skia", args.build_type, llvm_path, depot_tools, args.target_cpu, sccache)
    
    # Build main project
    if not args.skip_main:
        print("=" * 60)
        print("Building Main Project")
        print("=" * 60)
        
        build_main_project(
            script_dir, args.build_type,
            clang, clang_pp, sccache, vulkan_sdk,
            args.clean, args.cmake_args
        )
    
    # Summary
    print()
    print("=" * 60)
    print("Build Complete!")
    print("=" * 60)
    
    exe_name = "skia-renderer.exe" if platform.system() == "Windows" else "skia-renderer"
    exe_path = script_dir / "build" / args.build_type / exe_name
    
    if exe_path.exists():
        print(f"Executable: {exe_path}")
        print(f"\nRun: {exe_path}")
    
    print()
    return 0

if __name__ == "__main__":
    sys.exit(main())
