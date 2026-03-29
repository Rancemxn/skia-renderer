import os
import sys
import shutil
import subprocess
import argparse
import stat
import platform
import signal
from pathlib import Path

_running_processes = []

def _signal_handler(signum, frame):
    """Handle Ctrl+C by terminating all child processes"""
    print("\n\nInterrupted! Terminating build processes...")
    for proc in _running_processes:
        try:
            if proc.poll() is None:  # Still running
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
        except Exception:
            pass
    sys.exit(1)

# Register signal handler
signal.signal(signal.SIGINT, _signal_handler)
if platform.system() != "Windows":
    signal.signal(signal.SIGTERM, _signal_handler)

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
    """Find LLVM installation, preferring clang-cl on Windows"""
    # On Windows, prefer clang-cl for MSVC compatibility
    if platform.system() == "Windows":
        clang_cl = find_tool("clang-cl")
        if clang_cl:
            clang_path = Path(clang_cl)
            llvm_path = clang_path.parent.parent
            return str(llvm_path), clang_cl, clang_cl
        
        # Fallback: check standard LLVM installation paths
        for base in [r"C:\Program Files\LLVM", r"C:\LLVM"]:
            llvm_path = Path(base)
            clang_cl_path = llvm_path / "bin" / "clang-cl.exe"
            if clang_cl_path.exists():
                return str(llvm_path), str(clang_cl_path), str(clang_cl_path)
    
    # Non-Windows or clang-cl not found: use regular clang
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
    """Find gn"""
    if platform.system() == "Windows":
        gn_bat = depot_tools / "gn.bat"
        if gn_bat.exists():
            return str(gn_bat)
        print(f"  WARNING: gn.bat not found in {depot_tools}")
    gn = find_tool("gn", [str(depot_tools)])
    return gn

def find_gclient(depot_tools: Path) -> str:
    """Find gclient executable"""
    if platform.system() == "Windows":
        gclient = depot_tools / "gclient.bat"
    else:
        gclient = depot_tools / "gclient"
    
    if gclient.exists():
        return str(gclient)
    return None

# ========================================
# Utility Functions
# ========================================

def run_cmd(cmd: list, cwd: str = None, check: bool = True, env: dict = None) -> subprocess.CompletedProcess:
    """Run a command with proper Ctrl+C handling"""
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    
    print(f"  Running: {' '.join(str(c) for c in cmd)}")
    
    # Use Popen for better process control
    proc = subprocess.Popen(
        cmd, 
        cwd=cwd, 
        env=merged_env,
    )
    _running_processes.append(proc)
    
    try:
        result = proc.wait()
        _running_processes.remove(proc)
        
        if check and result != 0:
            raise subprocess.CalledProcessError(result, cmd)
        return subprocess.CompletedProcess(cmd, result)
    except KeyboardInterrupt:
        print(f"\nInterrupted")
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        raise

def remove_readonly(func, path, _):
    os.chmod(path, stat.S_IWRITE)
    func(path)

# ========================================
# Build Functions
# ========================================

def build_sdl3(source_dir: Path, build_type: str, 
               clang: str, clang_pp: str, sccache: str = None,
               verbose: bool = False) -> bool:
    """Build SDL3 with CMake -> deps/SDL3/out/{Debug,Release}/"""
    print("\n[SDL3]")
    
    if not source_dir.exists():
        print(f"  ERROR: SDL3 source not found: {source_dir}")
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
    build_cmd = ["cmake", "--build", str(build_dir), "--config", build_type]
    if verbose:
        build_cmd.append("--")  # Pass flags to ninja
        build_cmd.append("-v")  # Verbose ninja output
    run_cmd(build_cmd)
    
    print("  Installing...")
    run_cmd(["cmake", "--install", str(build_dir), "--config", build_type])
    
    print(f"  Output: {build_dir}")
    return True

def build_vkbootstrap(source_dir: Path, build_type: str,
                      clang: str, clang_pp: str, sccache: str = None,
                      verbose: bool = False) -> bool:
    """Build vk-bootstrap with CMake -> deps/vk-bootstrap/out/{Debug,Release}/"""
    print("\n[vk-bootstrap]")
    
    if not source_dir.exists():
        print(f"  ERROR: vk-bootstrap source not found: {source_dir}")
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
    build_cmd = ["cmake", "--build", str(build_dir), "--config", build_type]
    if verbose:
        build_cmd.append("--")
        build_cmd.append("-v")
    run_cmd(build_cmd)
    
    print("  Installing...")
    run_cmd(["cmake", "--install", str(build_dir), "--config", build_type])
    
    print(f"  Output: {build_dir}")
    return True

def build_angle(angle_dir: Path, build_type: str, llvm_path: str,
                depot_tools: Path, target_cpu: str, sccache: str = None,
                skip_angle: bool = False, verbose: bool = False) -> bool:
    """Build ANGLE with GN + Ninja -> deps/angle/out/{Debug,Release}/"""
    print("\n[ANGLE]")
    
    if not angle_dir.exists():
        print(f"  ERROR: ANGLE source not found: {angle_dir}")
        return False
    
    # Check if ANGLE is already built
    out_dir = angle_dir / "out" / build_type
    
    # ANGLE outputs: libEGL.dll.lib and libGLESv2.dll.lib on Windows
    # Check for both naming conventions
    if platform.system() == "Windows":
        egl_lib = out_dir / "libEGL.dll.lib"
        gles_lib = out_dir / "libGLESv2.dll.lib"
        # Also check for .dll files as a fallback
        egl_dll = out_dir / "libEGL.dll"
        gles_dll = out_dir / "libGLESv2.dll"
        angle_built = (egl_lib.exists() and gles_lib.exists()) or (egl_dll.exists() and gles_dll.exists())
    else:
        lib_ext = ".a"
        egl_lib = out_dir / f"libEGL{lib_ext}"
        gles_lib = out_dir / f"libGLESv2{lib_ext}"
        angle_built = egl_lib.exists() and gles_lib.exists()
    
    if skip_angle:
        if angle_built:
            print(f"  Skipping build (--skip-angle), using existing: {out_dir}")
            return True
        else:
            print("  WARNING: --skip-angle set but ANGLE not built, building anyway...")
    
    # Find tools
    gn = find_gn(depot_tools)
    if not gn:
        print("  ERROR: gn not found in depot_tools")
        return False
    
    ninja = find_ninja(depot_tools)
    if not ninja:
        print("  ERROR: ninja not found")
        return False
    
    # Setup environment
    env = os.environ.copy()
    env["PATH"] = str(depot_tools) + os.pathsep + env.get("PATH", "")
    env["DEPOT_TOOLS_UPDATE"] = "0"
    env["GCLIENT_PY3"] = "1"
    
    # On Windows, disable toolchain download
    if platform.system() == "Windows":
        env["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"
    
    gn_args = [
        f'target_cpu="{target_cpu}"',
        f'is_debug={"true" if build_type == "Debug" else "false"}',
        'is_component_build=false',
        'angle_build_all=false',  # Don't build tests/samples
        'angle_standalone=true',
        # Feature
        'angle_enable_cl=true',
        'angle_enable_essl=true',
        'angle_enable_glsl=true',
        'angle_enable_hlsl=true',
        'angle_enable_overlay=false',
        'angle_enable_vulkan_system_info=true',
        # Render
        'angle_build_vulkan_system_info=true',
        'angle_enable_vulkan=true',
        'angle_enable_gl=true',
        'angle_enable_d3d11=true',
        'angle_enable_d3d9=true',
        'angle_enable_wgpu=true',
        'dawn_enable_d3d11=true',
        'dawn_enable_d3d12=true',
        'dawn_enable_desktop_gl=true',
        'dawn_enable_opengles=true',
        'dawn_enable_vulkan=true',
        # Perf
        'angle_enable_abseil=true',
        'angle_enable_d3d11_compositor_native_window=true',
        # Debug
        f'angle_debug_layers_enabled={"true" if build_type == "Debug" else "false"}',
    ]
    
    if sccache:
        gn_args.append(f'cc_wrapper="{sccache}"')
    
    gn_args_str = " ".join(gn_args)
    
    # Build directory: deps/angle/out/{Debug,Release}
    out_dir = f"out/{build_type}"
    
    print("  Configuring...")
    run_cmd([gn, "gen", out_dir, f"--args={gn_args_str}"], cwd=str(angle_dir), env=env)
    
    print("  Building...")
    ninja_cmd = [ninja, "-C", out_dir, "libEGL", "libGLESv2"]
    if verbose:
        ninja_cmd.append("-v")
    run_cmd(ninja_cmd, cwd=str(angle_dir), env=env)
    
    print(f"  Output: {angle_dir / out_dir}")
    return True

def patch_dawn_cmake_utils(dawn_dir: Path) -> bool:
    """Fix Dawn's cmake_utils.py to handle Windows command line length limit.
    
    The discover_dependencies function passes too many arguments to ninja -tinputs,
    causing "The syntax of the command is incorrect" error on Windows.
    This function patches it to use batched processing.
    """
    cmake_utils_path = dawn_dir / "cmake_utils.py"
    
    if not cmake_utils_path.exists():
        print(f"  WARNING: cmake_utils.py not found at {cmake_utils_path}")
        return False
    
    content = cmake_utils_path.read_text(encoding='utf-8')
    
    # Check if already patched
    if "_discover_dependencies_batched" in content:
        return True  # Already patched
    
    # Find the discover_dependencies function
    import re
    
    # Pattern to match the problematic subprocess call
    # Looking for: subprocess.check_output([..., "-tinputs"] + build_targets)
    pattern = r'(def discover_dependencies\([^)]*\)[^:]*:\s*\n\s*""".*?"""\s*\n)?(\s*)(cmd\s*=\s*\[[^\]]*-tinputs[^\]]*\]\s*\+[^#\n]*)'
    
    match = re.search(pattern, content, re.DOTALL)
    
    if not match:
        # Try alternative pattern
        pattern2 = r'(\s+)(cmd\s*=\s*\[.*?-tinputs.*?\]\s*\+[^#\n]+)'
        match = re.search(pattern2, content)
    
    if match:
        indent = match.group(1) if match.lastindex else "    "
        
        # Create the replacement code
        replacement = f'''{indent}# PATCHED: Use batched processing on Windows to avoid command line length limit
{indent}import platform
{indent}
{indent}if platform.system() == "Windows" and len(build_targets) > 30:
{indent}    return _discover_dependencies_batched(build_dir, build_targets)
{indent}
{indent}cmd = [NINJA_EXE if 'NINJA_EXE' in dir() else "ninja", "-C", build_dir, "-tinputs"] + build_targets
'''
        
        # Apply the patch
        new_content = content[:match.start()] + replacement + content[match.end():]
        
        # Add the batched function if not present
        if "_discover_dependencies_batched" not in new_content:
            batched_func = '''

def _discover_dependencies_batched(build_dir: str, build_targets: list, batch_size: int = 30) -> tuple:
    """Batched version to avoid Windows command line length limit."""
    import subprocess
    
    all_dependencies = set()
    all_object_files = []
    ninja_exe = NINJA_EXE if 'NINJA_EXE' in dir() else "ninja"
    
    for i in range(0, len(build_targets), batch_size):
        batch = build_targets[i:i + batch_size]
        cmd = [ninja_exe, "-C", build_dir, "-tinputs"] + batch
        
        try:
            inputs = subprocess.check_output(cmd, stderr=subprocess.STDOUT).decode("utf-8").splitlines()
            for line in inputs:
                line = line.strip()
                if line and not line.startswith("#"):
                    if line.endswith(".obj") or line.endswith(".o"):
                        all_object_files.append(line)
                    else:
                        all_dependencies.add(line)
        except subprocess.CalledProcessError as e:
            print(f"Warning: batch query failed: {e}")
            continue
    
    return list(all_dependencies), all_object_files
'''
            # Insert before the last line
            new_content = new_content.rstrip() + batched_func + "\n"
        
        # Write the patched file
        cmake_utils_path.write_text(new_content, encoding='utf-8')
        return True
    
    return False


def build_skia(skia_dir: Path, build_type: str, llvm_path: str,
               depot_tools: Path, target_cpu: str, sccache: str = None,
               verbose: bool = False) -> bool:
    """Build Skia with GN + Ninja -> deps/skia/out/{Debug,Release}/"""
    print("\n[Skia]")
    
    if not skia_dir.exists():
        print(f"  ERROR: Skia source not found: {skia_dir}")
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
    
    # Patch Dawn's cmake_utils.py to fix Windows command line length limit
    dawn_dir = skia_dir / "third_party" / "dawn"
    if dawn_dir.exists():
        if patch_dawn_cmake_utils(dawn_dir):
            print("  Patched: Dawn cmake_utils.py (Windows cmd length fix)")
    
    # Setup environment
    env = os.environ.copy()
    env["PATH"] = str(depot_tools) + os.pathsep + env.get("PATH", "")
    
    # GN args
    is_windows = platform.system() == "Windows"
    crt_flag = "/MTd" if build_type == "Debug" else "/MT"
    
    gn_args = [
        # Basic
        f'target_cpu="{target_cpu}"',
        'cc="clang"', 'cxx="clang++"',
        f'is_official_build={"true" if build_type == "Release" else "false"}',
        f'is_debug={"true" if build_type == "Debug" else "false"}',
        'is_component_build=false', # MT static linking
        # Render
        'skia_enable_ganesh=true',
        'skia_enable_graphite=true',
        'skia_use_vulkan=true',
        'skia_use_gl=true',
        'skia_use_angle=true',
        'skia_use_dawn=true',
        'dawn_enable_d3d11=true',
        'dawn_enable_d3d12=true',
        'dawn_enable_opengles=true',
        'dawn_enable_vulkan=true',
        'skia_use_epoxy_egl=false',
        # Use external codec
        'skia_use_system_expat=false',
        'skia_use_system_harfbuzz=false',
        'skia_use_system_icu=false',
        'skia_use_system_libpng=false',
        'skia_use_system_zlib=false',
        'skia_use_system_freetype2=false',
        "skia_use_libavif=false",
        "skia_use_libwebp_encode=false",
        "skia_use_libwebp_decode=false",
        "skia_use_libpng_encode=false",
        "skia_use_libpng_decode=false",
        "skia_use_libjpeg_turbo_encode=false",
        "skia_use_libjpeg_turbo_decode=false",
        "skia_use_libjxl_decode=false",
        "skia_use_crabbyavif=false",
        "skia_use_dng_sdk=false",
        # Text
        'skia_enable_skparagraph=true',
        'skia_enable_skshaper=true',
        'skia_enable_skunicode=true',
        'skia_use_icu=true',
        'skia_use_bidi=true',
        'skia_use_harfbuzz=true',
        # Perf
        'skia_compile_modules=true',
        'skia_enable_tools=false',
        'skia_enable_optimize_size=false',
        'skia_enable_precompile=true',
        'skia_include_multiframe_procs=true',
        # Features
        'skia_enable_pdf=true',
        'skia_enable_svg=true',
        'skia_enable_skottie=true',
        'skia_use_freetype=true',
        'skia_use_expat=true',
        'skia_use_zlib=true',
        'skia_use_wuffs=true',
        'skia_use_vma=true',
        'skia_pdf_subset_harfbuzz=true',
        # Debug
        f'skia_enable_vulkan_debug_layers={"true" if build_type == "Debug" else "false"}',
        f'skia_enable_spirv_validation={"true" if build_type == "Debug" else "false"}',
    ]

    # Perf for Different OS
    if is_windows:
        # Render
        gn_args.append('skia_use_direct3d=true')

        # Font
        gn_args.append('skia_enable_fontmgr_win=true')
    
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
    ninja_cmd = [ninja, "-C", out_dir]
    if verbose:
        ninja_cmd.append("-v")
    run_cmd(ninja_cmd, cwd=str(skia_dir), env=env)
    
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
                       cmake_args: str = None, verbose: bool = False) -> bool:
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
    
    # Check dependencies (verbose mode shows details)
    if verbose:
        print("  Checking dependencies...")
        sdl3_built, sdl3_config_dir = check_sdl3_built(sdl3_out_dir)
        if not sdl3_built:
            print(f"  WARNING: SDL3 not found at {sdl3_out_dir}")
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
    else:
        # Simple dependency check (non-verbose)
        sdl3_built, sdl3_config_dir = check_sdl3_built(sdl3_out_dir)
        skia_lib = skia_dir / "out" / build_type / "skia.lib"
        skia_lib_alt = skia_dir / "out" / build_type / "libskia.a"
        skia_found = skia_lib.exists() or skia_lib_alt.exists()
        
        deps_ok = sdl3_built and skia_found
        if deps_ok:
            print("  Dependencies: OK")
        else:
            print("  Dependencies: Some missing (use -v for details)")
    
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
    
    # Enable verbose CMake dependency output
    if verbose:
        cmd.append("-DVERBOSE_DEPS=ON")
    else:
        cmd.append("-DVERBOSE_DEPS=OFF")
    
    # Print CMake variables only in verbose mode
    if verbose:
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
    build_cmd = ["cmake", "--build", str(build_dir), "--config", build_type]
    if verbose:
        build_cmd.append("--")
        build_cmd.append("-v")
    run_cmd(build_cmd)
    
    # Copy ANGLE DLLs to build directory
    if platform.system() == "Windows":
        angle_out_dir = deps_dir / "angle" / "out" / build_type
        
        # Find all DLL files in ANGLE output directory
        angle_dlls = []
        for dll in angle_out_dir.glob("*.dll"):
            angle_dlls.append(dll.name)
        
        if angle_dlls:
            print(f"  Copying ANGLE DLLs ({len(angle_dlls)} files):")
            for dll_name in sorted(angle_dlls):
                src = angle_out_dir / dll_name
                dst = build_dir / dll_name
                if dst.exists():
                    dst.unlink()
                shutil.copy2(str(src), str(dst))
                print(f"    {dll_name}")
        else:
            print("  WARNING: No ANGLE DLLs found")
    
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
    parser.add_argument("--skip-angle", action="store_true", help="Skip ANGLE")
    parser.add_argument("--skip-skia", action="store_true", help="Skip Skia")
    
    # Other options
    parser.add_argument("--vulkan-sdk", help="Vulkan SDK path")
    parser.add_argument("--cmake-args", help="Extra CMake arguments for main project")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show detailed dependency info")
    
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
    
    # Show compiler type
    if platform.system() == "Windows" and "clang-cl" in clang:
        print(f"  Compiler: clang-cl (MSVC-compatible)")
    else:
        print(f"  Compiler: {clang}")
    
    ninja = find_ninja(depot_tools)
    if not ninja:
        print("  ERROR: Ninja not found")
        return 1
    print(f"  Ninja: {ninja}")
    
    sccache = find_sccache()
    if sccache:
        print(f"  sccache: {sccache}")
    else:
        print("  sccache: not found")
    
    print()
    
    vulkan_sdk = args.vulkan_sdk or os.environ.get("VULKAN_SDK")
    
    # Build dependencies
    if not args.skip_deps:
        print("=" * 60)
        print("Building Dependencies")
        print("=" * 60)
        
        if not args.skip_sdl:
            build_sdl3(deps_dir / "SDL3", args.build_type, clang, clang_pp, sccache,
                      verbose=args.verbose)
        
        if not args.skip_vkbootstrap:
            build_vkbootstrap(deps_dir / "vk-bootstrap", args.build_type, clang, clang_pp, sccache,
                             verbose=args.verbose)
        
        # Build ANGLE (can be skipped with --skip-angle)
        build_angle(
            deps_dir / "angle", args.build_type, llvm_path, depot_tools,
            args.target_cpu, sccache, skip_angle=args.skip_angle,
            verbose=args.verbose
        )
        
        if not args.skip_skia:
            build_skia(deps_dir / "skia", args.build_type, llvm_path, depot_tools, 
                      args.target_cpu, sccache, verbose=args.verbose)
    
    # Build main project
    if not args.skip_main:
        print("=" * 60)
        print("Building Main Project")
        print("=" * 60)
        
        build_main_project(
            script_dir, args.build_type,
            clang, clang_pp, sccache, vulkan_sdk,
            args.clean, args.cmake_args, args.verbose
        )
    
    # Summary
    print()
    print("=" * 60)
    print("Build Complete")
    print("=" * 60)
    
    exe_name = "skia-renderer.exe" if platform.system() == "Windows" else "skia-renderer"
    exe_path = script_dir / "build" / args.build_type / exe_name
    
    if exe_path.exists():
        print(f"Executable: {exe_path}")
    
    print()
    return 0

if __name__ == "__main__":
    sys.exit(main())
