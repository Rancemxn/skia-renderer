import os
import sys
import shutil
import subprocess
import argparse
import platform
import tarfile
import zipfile
from pathlib import Path

# Default versions
SDL3_VERSION = "3.4.2"
VKBOOTSTRAP_VERSION = "1.4.343"
SPDLOG_VERSION = "1.17.0"
CLI11_VERSION = "2.6.2"
DIRECTX_HEADERS_REPO = "https://github.com/microsoft/DirectX-Headers.git"

# ANGLE configuration
ANGLE_COMMIT = "ae66dc5ad3506d3ea7196da4dba54a7b1f8b4f8c"
ANGLE_REPO_URL = "https://github.com/google/angle.git"

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
    
    # Check 7-Zip on Windows
    if name == "7z" and platform.system() == "Windows":
        for p in [r"C:\Program Files\7-Zip", r"C:\Program Files (x86)\7-Zip"]:
            exe = Path(p) / "7z.exe"
            if exe.exists():
                return str(exe)
    
    return None

def run_cmd(cmd: list, cwd: str = None, check: bool = True, env: dict = None, verbose: bool = False) -> subprocess.CompletedProcess:
    """Run a command"""
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    
    print(f"  Running: {' '.join(str(c) for c in cmd)}", flush=True)
    result = subprocess.run(cmd, cwd=cwd, env=merged_env)
    if check and result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, cmd)
    return result

# ========================================
# Download Functions
# ========================================

def download_with_aria2(url: str, output_dir: Path, filename: str, 
                        proxy: str = None, verbose: bool = False) -> Path:
    """Download using aria2c (fast, multi-threaded)"""
    aria2 = find_tool("aria2c")
    if not aria2:
        return None
    
    output_path = output_dir / filename
    
    cmd = [
        aria2, "-x", "16", "-s", "32", "-k", "1M",
        "--check-certificate=false",
        "--file-allocation=none",
        "--allow-overwrite=true",
        "--auto-file-renaming=false",
        "-d", str(output_dir),
        "-o", filename,
        url
    ]
    
    if verbose:
        cmd.append("-v")  # aria2 verbose mode
    
    if proxy:
        cmd.extend(["--all-proxy", proxy])
    
    print(f"  Downloading (aria2): {filename}", flush=True)
    run_cmd(cmd, verbose=verbose)
    
    return output_path if output_path.exists() else None

def download_with_curl(url: str, output_dir: Path, filename: str,
                       proxy: str = None, verbose: bool = False) -> Path:
    """Download using curl (fallback)"""
    curl = find_tool("curl")
    if not curl:
        return None
    
    output_path = output_dir / filename
    
    cmd = [
        curl, "-L", "-o", str(output_path),
        "--connect-timeout", "30",
        "--retry", "3",
    ]
    
    if verbose:
        cmd.append("-v")  # curl verbose mode
    
    if proxy:
        cmd.extend(["--proxy", proxy])
    
    cmd.append(url)
    
    print(f"  Downloading (curl): {filename}", flush=True)
    run_cmd(cmd, verbose=verbose)
    
    return output_path if output_path.exists() else None

def download_with_wget(url: str, output_dir: Path, filename: str,
                       proxy: str = None, verbose: bool = False) -> Path:
    """Download using wget (fallback)"""
    wget = find_tool("wget")
    if not wget:
        return None
    
    output_path = output_dir / filename
    
    cmd = [wget, "-O", str(output_path)]
    
    if verbose:
        cmd.append("-v")  # wget verbose mode
    
    if proxy:
        cmd.extend(["-e", f"http_proxy={proxy}", "-e", f"https_proxy={proxy}"])
    
    cmd.append(url)
    
    print(f"  Downloading (wget): {filename}", flush=True)
    run_cmd(cmd, verbose=verbose)
    
    return output_path if output_path.exists() else None

def download_file(url: str, output_dir: Path, filename: str,
                  proxy: str = None, verbose: bool = False) -> Path:
    """Download a file using available tool"""
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / filename
    
    # Skip if already downloaded
    if output_path.exists():
        print(f"  Already exists: {filename}", flush=True)
        return output_path
    
    # Try tools in order of preference
    result = None
    
    result = download_with_aria2(url, output_dir, filename, proxy, verbose)
    if result:
        return result
    
    result = download_with_curl(url, output_dir, filename, proxy, verbose)
    if result:
        return result
    
    result = download_with_wget(url, output_dir, filename, proxy, verbose)
    if result:
        return result
    
    raise RuntimeError("No download tool found. Install aria2, curl, or wget.")

# ========================================
# Extraction Functions
# ========================================

def extract_with_7z(archive: Path, output_dir: Path, strip_components: int = 1, verbose: bool = False) -> Path:
    """Extract using 7z"""
    sevenzip = find_tool("7z")
    if not sevenzip:
        return None
    
    print(f"  Extracting (7z): {archive.name}", flush=True)
    
    if strip_components == 0:
        cmd = [sevenzip, "x", "-y", f"-o{output_dir}", str(archive)]
        run_cmd(cmd)
        return output_dir
    
    # Extract to temp, then move
    temp_dir = output_dir.parent / f"{output_dir.name}_temp"
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    temp_dir.mkdir(parents=True)
    
    cmd = [sevenzip, "x", "-y", f"-o{temp_dir}", str(archive)]
    run_cmd(cmd)
    
    # Navigate through strip_components
    src_dir = temp_dir
    for _ in range(strip_components):
        subdirs = [d for d in src_dir.iterdir() if d.is_dir()]
        if len(subdirs) == 1:
            src_dir = subdirs[0]
        else:
            break
    
    # Move to final location
    if output_dir.exists():
        shutil.rmtree(output_dir)
    shutil.move(str(src_dir), str(output_dir))
    
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    
    return output_dir

def extract_with_unzip(archive: Path, output_dir: Path, strip_components: int = 1, verbose: bool = False) -> Path:
    """Extract zip using unzip"""
    unzip = find_tool("unzip")
    if not unzip or not str(archive).endswith('.zip'):
        return None
    
    print(f"  Extracting (unzip): {archive.name}", flush=True)
    
    temp_dir = output_dir.parent / f"{output_dir.name}_temp"
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    temp_dir.mkdir(parents=True)
    
    cmd = [unzip, "-q", "-o", str(archive), "-d", str(temp_dir)]
    run_cmd(cmd)
    
    # Navigate through strip_components
    src_dir = temp_dir
    for _ in range(strip_components):
        subdirs = [d for d in src_dir.iterdir() if d.is_dir()]
        if len(subdirs) == 1:
            src_dir = subdirs[0]
        else:
            break
    
    if output_dir.exists():
        shutil.rmtree(output_dir)
    shutil.move(str(src_dir), str(output_dir))
    
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    
    return output_dir

def extract_with_tar(archive: Path, output_dir: Path, strip_components: int = 1, verbose: bool = False) -> Path:
    """Extract tar archives using tar command"""
    tar = find_tool("tar")
    if not tar:
        return None
    
    name = str(archive).lower()
    if not (name.endswith('.tar.gz') or name.endswith('.tgz') or 
            name.endswith('.tar.bz2') or name.endswith('.tar.xz') or 
            name.endswith('.tar')):
        return None
    
    print(f"  Extracting (tar): {archive.name}", flush=True)
    
    if output_dir.exists():
        shutil.rmtree(output_dir)
    
    # tar can handle strip-components natively
    cmd = [tar, "-xf", str(archive), "-C", str(output_dir.parent)]
    if strip_components > 0:
        cmd.extend([f"--strip-components={strip_components}"])
    
    # Rename extracted dir to target name
    temp_name = output_dir.parent / "extract_temp"
    if temp_name.exists():
        shutil.rmtree(temp_name)
    
    run_cmd(cmd)
    
    # Find extracted directory
    for item in output_dir.parent.iterdir():
        if item.is_dir() and item != temp_name:
            if item.name.startswith("extract_temp"):
                continue
            shutil.move(str(item), str(output_dir))
            break
    
    return output_dir

def extract_with_python(archive: Path, output_dir: Path, strip_components: int = 1, verbose: bool = False) -> Path:
    """Extract using Python's built-in modules (fallback)"""
    print(f"  Extracting (Python): {archive.name}", flush=True)
    
    temp_dir = output_dir.parent / f"{output_dir.name}_temp"
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    temp_dir.mkdir(parents=True)
    
    name = str(archive).lower()
    
    try:
        if name.endswith('.zip'):
            with zipfile.ZipFile(archive, 'r') as zf:
                zf.extractall(temp_dir)
        elif name.endswith('.tar.gz') or name.endswith('.tgz'):
            with tarfile.open(archive, 'r:gz') as tf:
                tf.extractall(temp_dir)
        elif name.endswith('.tar.bz2'):
            with tarfile.open(archive, 'r:bz2') as tf:
                tf.extractall(temp_dir)
        elif name.endswith('.tar.xz'):
            with tarfile.open(archive, 'r:xz') as tf:
                tf.extractall(temp_dir)
        elif name.endswith('.tar'):
            with tarfile.open(archive, 'r') as tf:
                tf.extractall(temp_dir)
        else:
            return None
    except Exception as e:
        print(f"  Python extraction failed: {e}", flush=True)
        return None
    
    # Navigate through strip_components
    src_dir = temp_dir
    for _ in range(strip_components):
        subdirs = [d for d in src_dir.iterdir() if d.is_dir()]
        if len(subdirs) == 1:
            src_dir = subdirs[0]
        else:
            break
    
    if output_dir.exists():
        shutil.rmtree(output_dir)
    shutil.move(str(src_dir), str(output_dir))
    
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    
    return output_dir

def extract_archive(archive: Path, output_dir: Path, strip_components: int = 1, verbose: bool = False) -> Path:
    """Extract an archive using available tool"""
    result = None
    
    # Try 7z first (handles all formats)
    result = extract_with_7z(archive, output_dir, strip_components, verbose)
    if result:
        return result
    
    # Try native tar
    result = extract_with_tar(archive, output_dir, strip_components, verbose)
    if result:
        return result
    
    # Try unzip for zip files
    result = extract_with_unzip(archive, output_dir, strip_components, verbose)
    if result:
        return result
    
    # Try Python's built-in (last resort)
    result = extract_with_python(archive, output_dir, strip_components, verbose)
    if result:
        return result
    
    raise RuntimeError(f"No extraction tool found for {archive.name}")

# ========================================
# Git Clone
# ========================================

def git_clone(url: str, target_dir: Path, depth: int = 1, branch: str = None, verbose: bool = False) -> Path:
    """Clone a git repository"""
    git = find_tool("git")
    if not git:
        raise RuntimeError("git not found")
    
    if target_dir.exists():
        print(f"  Removing existing: {target_dir}", flush=True)
        shutil.rmtree(target_dir)
    
    cmd = [git, "clone"]
    if depth:
        cmd.extend(["--depth", str(depth)])
    if branch:
        cmd.extend(["--branch", branch])
    if verbose:
        cmd.append("--verbose")  # git verbose output
    cmd.extend([url, str(target_dir)])
    
    print(f"  Cloning: {url}", flush=True)
    run_cmd(cmd, verbose=verbose)
    
    return target_dir

def git_clone_at_commit(url: str, target_dir: Path, commit: str, depth: int = 1, verbose: bool = False) -> Path:
    """Clone a git repository at a specific commit"""
    git = find_tool("git")
    if not git:
        raise RuntimeError("git not found")
    
    if target_dir.exists():
        print(f"  Removing existing: {target_dir}", flush=True)
        shutil.rmtree(target_dir)
    
    # Clone with depth=1 for shallow clone, then fetch the specific commit
    print(f"  Cloning: {url}", flush=True)
    cmd = [git, "clone", "--no-checkout"]
    if depth:
        cmd.extend(["--depth", str(depth)])
    if verbose:
        cmd.append("--verbose")
    cmd.extend([url, str(target_dir)])
    run_cmd(cmd, verbose=verbose)
    
    # Fetch the specific commit
    print(f"  Fetching commit: {commit[:8]}", flush=True)
    if depth:
        cmd = [git, "fetch", "--depth", "1", "origin", commit]
    else:
        cmd = [git, "fetch", "origin", commit]
    if verbose:
        cmd.append("--verbose")
    run_cmd(cmd, cwd=str(target_dir), verbose=verbose)
    
    # Checkout the commit
    cmd = [git, "checkout", commit]
    run_cmd(cmd, cwd=str(target_dir), verbose=verbose)
    
    return target_dir

# ========================================
# Main
# ========================================

def sync_deps(args):
    """Main sync function"""
    script_dir = Path(__file__).parent.resolve()
    deps_dir = script_dir / "deps"
    downloads_dir = script_dir / "downloads"
    verbose = args.verbose
    
    deps_dir.mkdir(parents=True, exist_ok=True)
    downloads_dir.mkdir(parents=True, exist_ok=True)
    
    print("=" * 50, flush=True)
    print("Skia Renderer - Dependency Sync", flush=True)
    print("=" * 50, flush=True)
    print(flush=True)
    
    # Check tools
    print("[Checking Tools]", flush=True)
    
    tools = {
        "git": find_tool("git"),
    }
    
    # Download tools
    download_tools = []
    if find_tool("aria2c"):
        download_tools.append("aria2c")
    if find_tool("curl"):
        download_tools.append("curl")
    if find_tool("wget"):
        download_tools.append("wget")
    
    # Extraction tools  
    extract_tools = []
    if find_tool("7z"):
        extract_tools.append("7z")
    if find_tool("tar"):
        extract_tools.append("tar")
    if find_tool("unzip"):
        extract_tools.append("unzip")
    # Python is always available
    extract_tools.append("Python")
    
    print(f"  git: {tools['git'] or 'NOT FOUND'}", flush=True)
    print(f"  Download: {', '.join(download_tools) or 'NONE'}", flush=True)
    print(f"  Extract: {', '.join(extract_tools)}", flush=True)
    print(f"  Python: {sys.executable}", flush=True)
    
    if not tools["git"]:
        print("\n  ERROR: git is required", flush=True)
        return 1
    
    if not download_tools:
        print("\n  ERROR: No download tool found (aria2c, curl, or wget)", flush=True)
        return 1
    
    print(flush=True)
    
    overwrite = not args.no_overwrite
    
    # 1. SDL3
    if not args.skip_sdl:
        print("=" * 50, flush=True)
        print("[1/7] SDL3", flush=True)
        print("=" * 50, flush=True)
        
        sdl_dir = deps_dir / "SDL3"
        
        if sdl_dir.exists() and not overwrite:
            print("  SDL3 already exists, skipping", flush=True)
        else:
            if sdl_dir.exists():
                shutil.rmtree(sdl_dir)
            
            url = f"https://github.com/libsdl-org/SDL/archive/refs/tags/release-{SDL3_VERSION}.zip"
            
            archive = download_file(url, downloads_dir, f"SDL3-{SDL3_VERSION}.zip",
                                   proxy=args.proxy, verbose=verbose)
            extract_archive(archive, sdl_dir, verbose=verbose)
            print("  [OK] SDL3", flush=True)
        print(flush=True)
    
    # 2. vk-bootstrap
    if not args.skip_vkbootstrap:
        print("=" * 50, flush=True)
        print("[2/7] vk-bootstrap", flush=True)
        print("=" * 50, flush=True)
        
        vkb_dir = deps_dir / "vk-bootstrap"
        
        if vkb_dir.exists() and not overwrite:
            print("  vk-bootstrap already exists, skipping", flush=True)
        else:
            if vkb_dir.exists():
                shutil.rmtree(vkb_dir)
            
            url = f"https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v{VKBOOTSTRAP_VERSION}.zip"
            archive = download_file(url, downloads_dir, f"vk-bootstrap-{VKBOOTSTRAP_VERSION}.zip",
                                   proxy=args.proxy, verbose=verbose)
            extract_archive(archive, vkb_dir, verbose=verbose)
            print("  [OK] vk-bootstrap", flush=True)
        print(flush=True)
    
    # 3. spdlog (header-only library)
    if not args.skip_spdlog:
        print("=" * 50, flush=True)
        print("[3/7] spdlog", flush=True)
        print("=" * 50, flush=True)
        
        spdlog_dir = deps_dir / "spdlog"
        
        if spdlog_dir.exists() and not overwrite:
            print("  spdlog already exists, skipping", flush=True)
        else:
            if spdlog_dir.exists():
                shutil.rmtree(spdlog_dir)
            
            url = f"https://github.com/gabime/spdlog/archive/refs/tags/v{SPDLOG_VERSION}.zip"
            archive = download_file(url, downloads_dir, f"spdlog-{SPDLOG_VERSION}.zip",
                                   proxy=args.proxy, verbose=verbose)
            extract_archive(archive, spdlog_dir, verbose=verbose)
            print("  [OK] spdlog", flush=True)
        print(flush=True)
    
    # 4. CLI11 (header-only library)
    if not args.skip_cli11:
        print("=" * 50, flush=True)
        print("[4/7] CLI11", flush=True)
        print("=" * 50, flush=True)
        
        cli11_dir = deps_dir / "CLI11"
        
        if cli11_dir.exists() and not overwrite:
            print("  CLI11 already exists, skipping", flush=True)
        else:
            if cli11_dir.exists():
                shutil.rmtree(cli11_dir)
            
            url = f"https://github.com/CLIUtils/CLI11/archive/refs/tags/v{CLI11_VERSION}.zip"
            archive = download_file(url, downloads_dir, f"CLI11-{CLI11_VERSION}.zip",
                                   proxy=args.proxy, verbose=verbose)
            extract_archive(archive, cli11_dir, verbose=verbose)
            print("  [OK] CLI11", flush=True)
        print(flush=True)

    # 5. DirectX-Headers (for D3D12 development on Windows)
    if not args.skip_directx_headers:
        print("=" * 50, flush=True)
        print("[5/7] DirectX-Headers", flush=True)
        print("=" * 50, flush=True)

        dxheaders_dir = deps_dir / "DirectX-Headers"

        if dxheaders_dir.exists() and not overwrite:
            print("  DirectX-Headers already exists, skipping", flush=True)
        else:
            if dxheaders_dir.exists():
                shutil.rmtree(dxheaders_dir)

            git_clone(DIRECTX_HEADERS_REPO, dxheaders_dir, depth=1, verbose=verbose)
            print("  [OK] DirectX-Headers", flush=True)
        print(flush=True)
    else:
        print("[5/7] DirectX-Headers - Skipped (--skip-directx-headers)", flush=True)
        print(flush=True)

    # 6. ANGLE (can be skipped with --skip-angle if already downloaded)
    if not args.skip_angle:
        print("=" * 50, flush=True)
        print("[6/7] ANGLE", flush=True)
        print("=" * 50, flush=True)
        
        angle_dir = deps_dir / "angle"
        depot_dir = deps_dir / "depot_tools"
        
        # Ensure depot_tools exists (needed for gclient)
        if not depot_dir.exists():
            depot_url = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"
            git_clone(depot_url, depot_dir, verbose=verbose)
        else:
            print(f"  depot_tools already exists: {depot_dir}", flush=True)
        
        if angle_dir.exists() and not overwrite:
            print("  ANGLE already exists, skipping", flush=True)
        else:
            # Remove existing ANGLE directory
            if angle_dir.exists():
                shutil.rmtree(angle_dir)
            angle_dir.mkdir(parents=True, exist_ok=True)
            
            # Setup environment
            env = os.environ.copy()
            env["PATH"] = str(depot_dir) + os.pathsep + env.get("PATH", "")
            env["DEPOT_TOOLS_UPDATE"] = "0"
            env["GCLIENT_PY3"] = "1"
            # Set GIT_CACHE_PATH to avoid Windows git.bat not found error
            git_cache_dir = deps_dir / "git_cache"
            git_cache_dir.mkdir(parents=True, exist_ok=True)
            env["GIT_CACHE_PATH"] = str(git_cache_dir)
            if platform.system() == "Windows":
                env["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"
            
            if platform.system() == "Windows":
                gclient = depot_dir / "gclient.bat"
            else:
                gclient = depot_dir / "gclient"
            
            # Configure gclient for ANGLE (let gclient manage the clone)
            print("  Configuring gclient for ANGLE...", flush=True)
            gclient_spec = f"solutions = [ {{ 'name': '.', 'url': 'https://chromium.googlesource.com/angle/angle.git@{ANGLE_COMMIT}', 'deps_file': 'DEPS', 'managed': True, 'custom_vars': {{}}, }}, ];"
            run_cmd([str(gclient), "config", "--spec", gclient_spec],
                   cwd=str(angle_dir), env=env, verbose=verbose)
            
            # Run gclient sync (this will clone ANGLE + all dependencies)
            print("  Running gclient sync for ANGLE...", flush=True)
            run_cmd([str(gclient), "sync", "--with_branch_heads", "--with_tags"],
                   cwd=str(angle_dir), env=env, check=False, verbose=verbose)
            
            # Run gclient runhooks
            print("  Running gclient runhooks...", flush=True)
            run_cmd([str(gclient), "runhooks"],
                   cwd=str(angle_dir), env=env, check=False, verbose=verbose)
            
            print(f"  [OK] ANGLE (commit {ANGLE_COMMIT[:8]})", flush=True)
        print(flush=True)
    else:
        print("[6/7] ANGLE - Skipped (--skip-angle)", flush=True)
        print(flush=True)

    # 7. Skia
    if not args.skip_skia:
        print("=" * 50, flush=True)
        print("[7/7] Skia", flush=True)
        print("=" * 50, flush=True)
        
        skia_dir = deps_dir / "skia"
        depot_dir = deps_dir / "depot_tools"
        
        # Clone depot_tools
        if not depot_dir.exists():
            depot_url = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"
            git_clone(depot_url, depot_dir, verbose=verbose)
        else:
            print(f"  depot_tools already exists: {depot_dir}", flush=True)
        
        # Clone Skia
        if skia_dir.exists() and not overwrite:
            print("  Skia already exists, skipping clone", flush=True)
        else:
            if skia_dir.exists():
                shutil.rmtree(skia_dir)
            
            skia_url = "https://skia.googlesource.com/skia.git"
            git_clone(skia_url, skia_dir, branch="chrome/m147", verbose=verbose)
        
        # Sync dependencies
        print("  Syncing Skia dependencies...", flush=True)
        sync_script = skia_dir / "tools" / "git-sync-deps"
        if sync_script.exists():
            try:
                run_cmd([sys.executable, str(sync_script)], cwd=str(skia_dir), check=False, verbose=verbose)
            except Exception as e:
                print(f"  Warning: Skia deps sync error: {e}", flush=True)
        else:
            print("  Warning: git-sync-deps not found", flush=True)
        
        print("  [OK] Skia", flush=True)
        print(flush=True)
    
    # Cleanup downloads
    if not args.keep_downloads:
        print("Cleaning up downloads...", flush=True)
        if downloads_dir.exists():
            shutil.rmtree(downloads_dir)
            print("  [OK] Downloads cleaned", flush=True)
        print(flush=True)
    
    # Summary
    print("=" * 50, flush=True)
    print("Dependency Sync Complete", flush=True)
    print("=" * 50, flush=True)
    print(flush=True)
    print("Dependencies downloaded:", flush=True)
    for name in ["SDL3", "vk-bootstrap", "spdlog", "CLI11", "DirectX-Headers", "angle", "skia", "depot_tools"]:
        if (deps_dir / name).exists():
            print(f"  [OK] {name}", flush=True)
    print(flush=True)
    return 0

def main():
    parser = argparse.ArgumentParser(description="Sync dependencies for Skia Renderer")
    
    # Sync options
    parser.add_argument("--skip-skia", action="store_true", help="Skip Skia")
    parser.add_argument("--skip-sdl", action="store_true", help="Skip SDL3")
    parser.add_argument("--skip-vkbootstrap", action="store_true", help="Skip vk-bootstrap")
    parser.add_argument("--skip-spdlog", action="store_true", help="Skip spdlog")
    parser.add_argument("--skip-cli11", action="store_true", help="Skip CLI11")
    parser.add_argument("--skip-angle", action="store_true", help="Skip ANGLE")
    parser.add_argument("--skip-directx-headers", action="store_true", help="Skip DirectX-Headers")
    parser.add_argument("--no-overwrite", action="store_true", help="Don't overwrite existing")
    
    # Download options
    parser.add_argument("--proxy", type=str, help="Proxy URL")
    parser.add_argument("--keep-downloads", action="store_true", help="Keep downloaded archives")
    
    # Verbose option
    parser.add_argument("-v", "--verbose", action="store_true", help="Enable verbose output")
    
    args = parser.parse_args()
    
    try:
        return sync_deps(args)
    except Exception as e:
        print(f"\nERROR: {e}", flush=True)
        return 1

if __name__ == "__main__":
    sys.exit(main())
