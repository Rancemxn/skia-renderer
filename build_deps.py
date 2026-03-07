"""
Skia Renderer - Build Dependencies
Builds SDL3, vk-bootstrap, Skia with LLVM/Clang + Ninja + sccache
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

def build_with_cmake(source_dir: Path, build_dir: Path, install_dir: Path,
                     build_type: str, clang: str, clang_pp: str,
                     sccache: str = None, extra_args: list = None) -> bool:
    """Build a project with CMake using LLVM/Clang + Ninja + sccache"""
    
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True)
    
    # CMake configure
    cmd = [
        "cmake", "-S", str(source_dir), "-B", str(build_dir), 
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_C_COMPILER={clang}",
        f"-DCMAKE_CXX_COMPILER={clang_pp}",
        f"-DCMAKE_INSTALL_PREFIX={install_dir}",
    ]
    
    # Add sccache as compiler launcher
    if sccache:
        cmd.append(f"-DCMAKE_C_COMPILER_LAUNCHER={sccache}")
        cmd.append(f"-DCMAKE_CXX_COMPILER_LAUNCHER={sccache}")
    
    if extra_args:
        cmd.extend(extra_args)
    
    print("  Configuring...")
    run_cmd(cmd)
    
    # Build
    print("  Building...")
    build_cmd = ["cmake", "--build", str(build_dir), "--config", build_type]
    run_cmd(build_cmd)
    
    # Install
    print("  Installing...")
    install_cmd = ["cmake", "--install", str(build_dir), "--config", build_type]
    run_cmd(install_cmd)
    
    return True

def build_skia(skia_dir: Path, build_type: str, skia_args: dict, 
               depot_tools: Path, sccache: str = None) -> bool:
    """Build Skia with gn + ninja using Clang + sccache"""
    
    # Add depot_tools to PATH
    env = os.environ.copy()
    env["PATH"] = str(depot_tools) + os.pathsep + env.get("PATH", "")
    
    # Setup sccache for Skia
    if sccache:
        env["SCCACHE_DIR"] = str(skia_dir / ".sccache")
        env["CC"] = f"{sccache} clang"
        env["CXX"] = f"{sccache} clang++"
    
    # Check for gn
    gn = find_tool("gn", [str(depot_tools)])
    if not gn:
        print("  ERROR: gn not found in depot_tools")
        return False
    
    # Check for ninja
    ninja = find_ninja(depot_tools)
    if not ninja:
        print("  ERROR: ninja not found")
        return False
    
    # Build GN args for Clang
    gn_args = []
    gn_args.append(f'target_cpu="{skia_args.get("target_cpu", "x64")}"')
    
    is_windows = platform.system() == "Windows"
    if is_windows and skia_args.get("clang_win"):
        gn_args.append(f'clang_win="{skia_args["clang_win"]}"')
    
    # Use sccache wrapper for Skia
    if sccache:
        gn_args.append(f'cc_wrapper="{sccache}"')
    
    gn_args.append('cc="clang"')
    gn_args.append('cxx="clang++"')
    
    if is_windows:
        extra_flags = ['"/GR"', '"/EHsc"']
    else:
        extra_flags = ['"-frtti"', '"-fexceptions"']
    gn_args.append(f'extra_cflags_cc={extra_flags}')
    
    # Standard Skia args
    bool_args = [
        ("is_official_build", skia_args.get("is_official_build", True)),
        ("is_debug", skia_args.get("is_debug", False)),
        ("skia_enable_ganesh", skia_args.get("skia_enable_ganesh", True)),
        ("skia_enable_graphite", skia_args.get("skia_enable_graphite", True)),
        ("skia_use_vulkan", skia_args.get("skia_use_vulkan", True)),
        ("skia_use_gl", skia_args.get("skia_use_gl", True)),
        ("skia_use_system_expat", False),
        ("skia_use_system_harfbuzz", False),
        ("skia_use_system_icu", False),
        ("skia_use_system_libjpeg_turbo", False),
        ("skia_use_system_libpng", False),
        ("skia_use_system_freetype2", False),
        ("skia_use_system_libwebp", False),
        ("skia_use_system_zlib", False),
        ("skia_enable_tools", skia_args.get("skia_enable_tools", False)),
        ("skia_enable_pdf", skia_args.get("skia_enable_pdf", True)),
        ("skia_use_angle", False),
    ]
    
    for name, value in bool_args:
        gn_args.append(f'{name}={"true" if value else "false"}')
    
    gn_args_str = " ".join(gn_args)
    
    print(f"  GN args: {gn_args_str}")
    
    out_dir = skia_dir / "out" / build_type
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)
    
    # Generate
    print("  Generating...")
    run_cmd([gn, "gen", f"out/{build_type}", f"--args={gn_args_str}"], cwd=str(skia_dir), env=env)
    
    # Build
    print("  Building...")
    run_cmd([ninja, "-C", f"out/{build_type}"], cwd=str(skia_dir), env=env)
    
    return True

def build_deps(args):
    """Main build function"""
    script_dir = Path(__file__).parent.resolve()
    deps_dir = script_dir / "deps"
    build_dir = script_dir / "build_deps"
    install_dir = deps_dir / "installed"
    depot_tools = deps_dir / "depot_tools"
    
    print("=" * 50)
    print("Skia Renderer - Building Dependencies")
    print("=" * 50)
    print()
    
    # Find LLVM (required)
    print("[Detecting Build Tools]")
    
    cmake = find_tool("cmake")
    if not cmake:
        print("  ERROR: CMake not found")
        return 1
    print(f"  [OK] CMake: {cmake}")
    
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
        print("  Install Ninja or ensure depot_tools is available")
        return 1
    print(f"  [OK] Ninja: {ninja}")
    
    # Find sccache (optional)
    sccache = find_sccache()
    if sccache:
        print(f"  [OK] sccache: {sccache}")
    else:
        print("  [INFO] sccache not found (optional, speeds up rebuilds)")
    
    print()
    
    build_type = args.build_type
    install_dir.mkdir(parents=True, exist_ok=True)
    
    # Build SDL3
    if not args.skip_sdl:
        print("=" * 50)
        print("[1/3] SDL3")
        print("=" * 50)
        
        sdl_dir = deps_dir / "SDL3"
        if not sdl_dir.exists():
            print("  ERROR: SDL3 not found. Run sync.py first.")
        elif not (sdl_dir / "CMakeLists.txt").exists():
            print("  SDL3 is prebuilt, skipping build")
        else:
            sdl_build = build_dir / "SDL3"
            try:
                build_with_cmake(
                    sdl_dir, sdl_build, install_dir, build_type,
                    clang, clang_pp, sccache,
                    ["-DSDL_VULKAN=ON", "-DSDL_OPENGL=ON", "-DSDL_TEST=OFF", "-DSDL_TESTS=OFF"]
                )
                print("  [OK] SDL3")
            except Exception as e:
                print(f"  ERROR: {e}")
        print()
    
    # Build vk-bootstrap
    if not args.skip_vkbootstrap:
        print("=" * 50)
        print("[2/3] vk-bootstrap")
        print("=" * 50)
        
        vkb_dir = deps_dir / "vk-bootstrap"
        if not vkb_dir.exists():
            print("  ERROR: vk-bootstrap not found. Run sync.py first.")
        else:
            vkb_build = build_dir / "vk-bootstrap"
            try:
                build_with_cmake(
                    vkb_dir, vkb_build, install_dir, build_type,
                    clang, clang_pp, sccache
                )
                print("  [OK] vk-bootstrap")
            except Exception as e:
                print(f"  ERROR: {e}")
        print()
    
    # Build Skia
    if not args.skip_skia:
        print("=" * 50)
        print("[3/3] Skia")
        print("=" * 50)
        
        skia_dir = deps_dir / "skia"
        if not skia_dir.exists():
            print("  ERROR: Skia not found. Run sync.py first.")
        else:
            skia_args = {
                "target_cpu": args.target_cpu,
                "clang_win": llvm_path,
                "is_official_build": args.build_type == "Release",
                "is_debug": args.build_type == "Debug",
                "skia_enable_tools": args.skia_tools,
                "skia_enable_graphite": True,
                "skia_use_vulkan": True,
            }
            
            try:
                build_skia(skia_dir, build_type, skia_args, depot_tools, sccache)
                print("  [OK] Skia")
            except Exception as e:
                print(f"  ERROR: {e}")
        print()
    
    # Summary
    print("=" * 50)
    print("Dependency Build Complete!")
    print("=" * 50)
    print()
    
    lib_ext = ".lib" if platform.system() == "Windows" else ".a"
    libs = [
        (install_dir / "lib" / f"SDL3{lib_ext}", f"SDL3{lib_ext}"),
        (install_dir / "lib" / f"SDL3d{lib_ext}", f"SDL3d{lib_ext} (Debug)"),
        (install_dir / "lib" / f"vk-bootstrap{lib_ext}", f"vk-bootstrap{lib_ext}"),
        (deps_dir / "skia" / "out" / build_type / f"skia{lib_ext}", f"skia{lib_ext}"),
    ]
    
    print("Built libraries:")
    for path, name in libs:
        if path.exists():
            print(f"  [OK] {name}")
    print()
    print("Next: python build.py")
    print()
    
    return 0

def main():
    parser = argparse.ArgumentParser(description="Build dependencies for Skia Renderer")
    
    # Build options
    parser.add_argument("--build-type", default="Release", choices=["Release", "Debug"],
                       help="Build type (default: Release)")
    parser.add_argument("--target-cpu", default="x64", help="Target CPU (default: x64)")
    
    # Skia options
    parser.add_argument("--skia-tools", action="store_true", help="Build Skia tools")
    
    # Skip options
    parser.add_argument("--skip-sdl", action="store_true", help="Skip SDL3")
    parser.add_argument("--skip-vkbootstrap", action="store_true", help="Skip vk-bootstrap")
    parser.add_argument("--skip-skia", action="store_true", help="Skip Skia")
    
    args = parser.parse_args()
    
    try:
        return build_deps(args)
    except Exception as e:
        print(f"\nERROR: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
